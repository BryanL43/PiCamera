#include "camera.h"

#include <future>
#include <cstring>
#include <memory>
#include <fstream>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>

static std::promise<void> requestPromise;

// Helper function to print metadata of a specific stream
std::string streamToString(Stream* stream) {
    if (!stream) {
        return "Invalid stream";
    }

    std::ostringstream oss;

    // Acquire width & height of stream
    auto size = stream->configuration().size;
    oss << "Stream Size: " << size.width << "x" << size.height << "\n";

    // Stream format (Pixel Format)
    auto format = stream->configuration().pixelFormat;
    oss << "Pixel Format: " << format.toString() << "\n";

    // Acquire number of buffers allocated for the stream
    oss << "Buffer Count: " << stream->configuration().bufferCount << "\n";

    return oss.str();
}

// Wait for camera request to be fulfilled
static void requestComplete(Request *request) {
    requestPromise.set_value();
	std::cout << "Request Completed." << std::endl;
}

// Camera initialization
void* cameraInit() {
    auto* cm = new CameraManager();
    if (cm->start() != 0) {
        std::cerr << "Failed to start camera manager." << std::endl;
        return nullptr;
    }
    return static_cast<void*>(cm);
}

// Capture a frame and encode it to JPEG
// Note: cm->stop() is not called on error. Will be passed to user for cameraTerminate().
int captureFrame(void* cmHandle) {
    // Acquire & validate CameraManager from C to C++ conversion
    auto* cm = static_cast<CameraManager*>(cmHandle);
    if (!cm) {
        std::cerr << "No valid camera manager handle." << std::endl;
        return -1;
    }

    // Identifies all cameras attached to the device
    auto cameras = cm->cameras();
    if (cameras.empty()) {
        std::cerr << "No cameras were identified on the system." << std::endl;
        return -2;
    }

    // Acquire only the first camera (only option we have) & put a lock on it
    std::shared_ptr<Camera> camera = cameras.front();
    if (camera->acquire() != 0) {
        std::cerr << "Failed to acquire camera." << std::endl;
        return -3;
    }
    std::cout << "Acquired camera: " << camera->id() << std::endl;

    // Create configuration profile for the camera
    std::unique_ptr<CameraConfiguration> config =
        camera->generateConfiguration({StreamRole::StillCapture});
    if (!config) {
        std::cerr << "Failed to create config profile for camera: " << camera->id() << std::endl;
        return -4;
    }
    std::cout << "Default configuration is: " << config->at(0).toString() << std::endl;

    // Adjust & validate the new resolution
    config->at(0).size = Size(640, 480);
    config->validate();
    if (camera->configure(config.get()) != 0) {
        std::cerr << "Failed to config camera: " << camera->id() << std::endl;
        return -5;
    }
    std::cout << "Validated configuration is: " << config->at(0).toString() << std::endl;

    // Create FrameBufferAllocator to allocate buffers from streams of a CameraConfiguration
    std::unique_ptr<FrameBufferAllocator> allocator =
        std::make_unique<FrameBufferAllocator>(camera);

    Stream* stream = config->at(0).stream();
    std::cout << streamToString(stream) << std::endl;

    // Allocate buffers for the stream
    if (allocator->allocate(stream) < 0) {
        std::cerr << "Failed to allocate buffers for stream." << std::endl;
        return -6; 
    }

    // Get the allocated buffers for the stream
    const std::vector<std::unique_ptr<FrameBuffer>> &buffers =
        allocator->buffers(stream);
    if (buffers.empty()) {
        std::cerr << "No buffers allocated" << std::endl;
        return -7;
    }

    // Start camera & create a request to access 1st frame
    if (camera->start() != 0) {
        std::cerr << "Failed to start camera: " << camera->id() << std::endl;
        return -8;
    }
    
    std::unique_ptr<Request> request = camera->createRequest();
    if (!request) {
        std::cerr << "Failed to create request for camera: " << camera->id() << std::endl;
        return -9;
    }

    // Add a buffer to the request
    if (request->addBuffer(stream, buffers[0].get()) < 0) {
        std::cerr << "Failed to add buffer to request" << std::endl;
        return -10;
    }

    // Create request completion event
    camera->requestCompleted.connect(requestComplete);

    // Queue the request for camera to populate with captured data
    if (camera->queueRequest(request.get()) != 0) {
        std::cerr << "Failed to queue request." << std::endl;
        return -11;
    }

    // Wait until request is fullfilled
    std::future<void> requestFuture = requestPromise.get_future();
    requestFuture.wait();

    // Acquire the 1st buffer (1st frame for still image)
    const libcamera::FrameBuffer* buffer = buffers[0].get();
    if (!buffer) {
        std::cerr << "No valid buffer." << std::endl;
        return -12;
    }

    // Obtain the 3 planes of YUV420: a color format that stores image data with luma
    // at full resolution and chroma channels at half resolution
    const auto& planes = buffer->planes();
    if (planes.size() < 3) {
        std::cerr << "Insufficient number of planes." << std::endl;
        return -13;
    }

    // Access the Y, U, and V planes directly
    const libcamera::FrameBuffer::Plane& y_plane = planes[0];
    const libcamera::FrameBuffer::Plane& u_plane = planes[1];
    const libcamera::FrameBuffer::Plane& v_plane = planes[2];

    // Calculate the total length of the YUV420 buffer
    size_t total_length = y_plane.length + u_plane.length + v_plane.length;

    // Map the entire buffer (all planes) in one shot
    void* buffer_data = mmap(nullptr, total_length, PROT_READ, MAP_SHARED, y_plane.fd.get(), 0);
    if (buffer_data == MAP_FAILED) {
        std::cerr << "Failed to map memory for all planes" << std::endl;
        return -14;
    }

    // Now you can access each plane by adding the appropriate aligned offset to the mapped buffer
    void* y_data = static_cast<uint8_t*>(buffer_data) + y_plane.offset;

    // YUV420 to BGR conversion using OpenCV
    const libcamera::Size& frameSize = config->at(0).size;
    int width = frameSize.width;
    int height = frameSize.height;

    // OpenCV image for YUV420p data
    cv::Mat yuv_image(height + height / 2, width, CV_8UC1, y_data);
    cv::Mat bgr_image;

    // Convert YUV420 to BGR using OpenCV
    cv::cvtColor(yuv_image, bgr_image, cv::COLOR_YUV2BGR_I420);

    // Save the image as a JPEG file
    std::string output_filename = "captured_image.jpg";
    bool result = cv::imwrite(output_filename, bgr_image);
    if (result) {
        std::cout << "Image created as " << output_filename << std::endl;
    } else {
        std::cerr << "Failed to create image." << std::endl;
    }

    // Release resources
    munmap(buffer_data, total_length);
    camera->stop();
    allocator->free(stream);
    camera->release();

    return 0;
}

// Terminate the camera
void cameraTerminate(void* cmHandle) {
    auto* cm = static_cast<CameraManager*>(cmHandle);
    if (cm) {
        cm->stop();
        delete cm;
        cm = nullptr;
    }
}