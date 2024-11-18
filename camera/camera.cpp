#include "camera.h"

static std::shared_ptr<Camera> camera;

// Helper function to print metadata of a specific stream
std::string streamToString(libcamera::Stream* stream) {
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

static void renderFrame(const libcamera::FrameBuffer &buffer,
                        const libcamera::StreamConfiguration &config) {
    
    // Acquire the first plane only
    const libcamera::FrameBuffer::Plane &plane = buffer.planes().front();

    try {
        // Map the plane to memory for opencv
        MapBuffer mappedBuffer(plane);
        
        // Convert the XRGB888 data to CV_8UC3 for opencv
        int width = config.size.width;
        int height = config.size.height;
        cv::Mat image(height, width, CV_8UC4, mappedBuffer.data());

        // Convert the XRGB888 to BGR
        cv::Mat bgr_image;
        cv::cvtColor(image, bgr_image, cv::COLOR_RGB2BGR);

        // Save the image as a JPEG file
        std::string output_filename = "captured_image.jpg";
        bool result = cv::imwrite(output_filename, bgr_image);
        if (result) {
            std::cout << "Image saved as " << output_filename << std::endl;
        } else {
            std::cerr << "Failed to save image." << std::endl;
        }

    } catch (const std::exception &e) {
        std::cerr << "Error rendering frame: " << e.what() << std::endl;
    }
}

// Wait for camera request to be fulfilled
static void requestComplete(libcamera::Request* request) {
    if (request->status() == libcamera::Request::RequestCancelled) {
        return;
    }

    // Map the request buffer containing the image's metadata and render the frame
    const std::map<const libcamera::Stream*, libcamera::FrameBuffer*> &buffers =
        request->buffers();
    
    for (auto &[stream, buffer] : buffers) {
        if (buffer->metadata().status == libcamera::FrameMetadata::FrameSuccess) {
            try {
                renderFrame(*buffer, stream->configuration());
            } catch (const std::exception &e) {
                std::cerr << "Error trying to render frame: " << e.what() << std::endl;
            }
        }
    }

    //
    request->reuse(libcamera::Request::ReuseBuffers);
    camera->queueRequest(request);
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
    camera = cameras.front();
    if (camera->acquire() != 0) {
        std::cerr << "Failed to acquire camera." << std::endl;
        return -3;
    }
    std::cout << "Acquired camera: " << camera->id() << std::endl;

    // Create configuration profile for the camera
    const std::unique_ptr<CameraConfiguration> config =
        camera->generateConfiguration({ StreamRole::VideoRecording });
    if (!config) {
        std::cerr << "Failed to create config profile for camera: " << camera->id() << std::endl;
        return -4;
    }
    StreamConfiguration &streamConfig = config->at(0);
    std::cout << "Default configuration is: " << streamConfig.toString() << std::endl;

    // Adjust & validate the new resolution
    streamConfig.size = Size(640, 480);
    streamConfig.pixelFormat = libcamera::formats::XRGB8888;
    config->validate();
    if (camera->configure(config.get()) != 0) {
        std::cerr << "Failed to config camera: " << camera->id() << std::endl;
        return -5;
    }
    std::cout << "Selected configuration is: " << streamConfig.toString() << std::endl;

    // Create FrameBufferAllocator to allocate buffers for streams of a CameraConfiguration
    FrameBufferAllocator* allocator = new FrameBufferAllocator(camera);
    for (StreamConfiguration &cfg : *config) {
        if (allocator->allocate(cfg.stream()) < 0) {
            std::cerr << "Failed to allocate buffers" << std::endl;
            return -6;
        }

        size_t allocated = allocator->buffers(cfg.stream()).size();
        std::cout << "Allocated " << allocated << " buffers for stream" << std::endl;
    }

    // Retrieves list of frame buffers & create request vectors to submit to the camera
    Stream* stream = streamConfig.stream();
    const std::vector<std::unique_ptr<FrameBuffer>> &buffers = allocator->buffers(stream);
    std::vector<std::unique_ptr<Request>> requests;

    std::cout << streamToString(stream) << std::endl;

    // Fill the request vector via Request instances from the camera 
    // & associate a buffer for each of them for the Stream
    for (unsigned int i = 0; i < buffers.size(); i++) {
        std::unique_ptr<Request> request = camera->createRequest();
        if (!request) {
            std::cerr << "Can't create request" << std::endl;
            return -7;
        }

        const std::unique_ptr<FrameBuffer> &buffer = buffers[i];
        if (request->addBuffer(stream, buffer.get()) < 0) {
            std::cerr << "Failed to set buffer for request" << std::endl;
            return -8;
        }

        requests.push_back(std::move(request));
    }

    // Create request completion event & start camera
    camera->requestCompleted.connect(requestComplete);

    if (camera->start() != 0) {
        std::cerr << "Failed to start camera: " << camera->id() << std::endl;
        return -9;
    }
    
    // Queue all previously created requests
    for (std::unique_ptr<Request> &request : requests) {
        if (camera->queueRequest(request.get()) != 0) {
            std::cerr << "Failed to queue request." << std::endl;
            return -10;
        }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(3000));
    
    // Release resources
    camera->stop();
    allocator->free(stream);
    delete allocator;
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