#include "CameraSensor.hpp"

CameraSensor::CameraSensor() {
    // Loads the library's camera manager for camera acquisition
    cameraManager = std::make_unique<CameraManager>();
    cameraManager->start();

    // Identifies all cameras attached to the device
    auto attachedCameras = cameraManager->cameras();
    if (attachedCameras.empty()) {
        std::cerr << "No cameras were identified on the system." << std::endl;
        cameraManager->stop();
        exit(EXIT_FAILURE);
    }

    // Acquire only the first camera (only option we have) & put a lock on it
    camera = attachedCameras.front();
    if (camera->acquire() != 0) {
        std::cerr << "Failed to acquire camera." << std::endl;
        cameraManager->stop();
        exit(EXIT_FAILURE);
    }
    std::cout << "Acquired camera: " << camera->id() << std::endl;

    // Create & config frame processor
    frameProcessor = std::make_unique<FrameProcessor>(5, 0.95, 90, 170, true);
}

CameraSensor::~CameraSensor() {
    camera->stop();
    camera->release();
    camera.reset();
    cameraManager->stop();
}

void CameraSensor::startCamera() {
    sendRequests();
    camera->requestCompleted.connect(this, &CameraSensor::requestComplete);
    camera->start();
    for (std::unique_ptr<Request>& request : requests) {
        camera->queueRequest(request.get());
    }
}

int CameraSensor::configCamera(const uint_fast32_t width, const uint_fast32_t height,
                                const PixelFormat pixelFormat, const StreamRole role) {
    // Create configuration profile for the camera
    config = camera->generateConfiguration({ role });
    StreamConfiguration &streamConfig = config->at(0);
    std::cout << "Default configuration is: " << streamConfig.toString() << std::endl;

    // Adjust & validate the desired configuration
    streamConfig.size.width = width;
    streamConfig.size.height = height;
    streamConfig.pixelFormat = pixelFormat;
    config->validate();
    if (camera->configure(config.get()) != 0) {
        std::cerr << "Failed to config camera: " << camera->id() << std::endl;
        return -EINVAL;
    }
    std::cout << "Selected configuration is: " << streamConfig.toString() << std::endl;

    // Allocate the buffers & map the memory we need for the incoming camera streams
    allocator = std::make_unique<FrameBufferAllocator>(camera);

    for (StreamConfiguration &cfg : *config) {
        // Allocate buffers via stream inputs
        Stream* stream = cfg.stream();
        if (allocator->allocate(cfg.stream()) < 0) {
            std::cerr << "Failed to allocate buffers" << std::endl;
            return -ENOMEM;
        }

        size_t allocated = allocator->buffers(cfg.stream()).size();
        std::cout << "Allocated " << allocated << " buffers for stream" << std::endl;

        // Pre-map the buffers so we don't recursively refresh memory regions when rendering
        // Note: Multi-plane buffering all have the same file descriptor
        // starting at 20, increasing
        for (const std::unique_ptr<FrameBuffer> &buffer : allocator->buffers(stream)) {
            // Iterate through all possible plane associated with a buffer
            // (i.e. YUV420 has 3; XRGB8888 has 1) 
            for (unsigned int i = 0; i < buffer->planes().size(); i++) {
                // Accounts for only YUV & XRGB8888 due to my integration for opencv.
                // Adjustments not accounted for other pixel formats.
                if (i == 0) {
                    // Maps the individual plane's buffer
                    const FrameBuffer::Plane &plane = buffer->planes()[i];
                    
                    void* data_ = mmap(NULL, plane.length, PROT_READ | PROT_WRITE, MAP_SHARED,
                                        plane.fd.get(), 0);
                    if (data_ == MAP_FAILED) {
                        throw std::runtime_error("Failed to map buffer for plane");
                    }

                    // Store mapped buffer for later use so we don't need to loop remapping
                    mappedBuffers[buffer.get()].push_back(
                        libcamera::Span<uint8_t>(static_cast<uint8_t*>(data_), plane.length)
                    );
                }
            }
            // Store the stream's buffer for request
            frameBuffers[stream].push(buffer.get());
        }
    }

    return 0;
}

void CameraSensor::sendRequests() {
    // Acquire the allocated buffers for streams stored in CameraConfiguration by libcamera
    // to create the requests (we can percieve request as a promise and fullfill event)
    for (StreamConfiguration &cfg : *config) {
        Stream* stream = cfg.stream();
        
        std::unique_ptr<Request> request = camera->createRequest();
        if (!request) {
            std::cerr << "Can't create request" << std::endl;
            throw std::runtime_error("Failed to make a request");
        }
        requests.push_back(std::move(request));

        // Seperate the frame buffer associated with the stream
        FrameBuffer* buffer = frameBuffers[stream].front();

        if (requests.back()->addBuffer(stream, buffer) < 0) {
            throw std::runtime_error("Failed to add buffer to request");
        }
    }
}

void CameraSensor::requestComplete(Request* request) {
    if (request->status() == Request::RequestCancelled) {
        return;
    }

    cv::Mat frame;
    const std::map<const Stream*, FrameBuffer*> &buffers =
        request->buffers();
    
    // Iterate through all the request's buffers & render its image frame
    for (auto &[stream, buffer] : buffers) {
        if (buffer->metadata().status == FrameMetadata::FrameSuccess) {
            try {
                renderFrame(frame, buffer);
                request->reuse(Request::ReuseBuffers);
                camera->queueRequest(request);
            } catch (const std::exception &e) {
                std::cerr << "Error trying to render frame: " << e.what() << std::endl;
            }
        }
    }
}

void CameraSensor::renderFrame(cv::Mat &frame, const libcamera::FrameBuffer *buffer) {
    const StreamConfiguration &streamConfig = config->at(0);

    try {
        // Find the mapped buffer associated with the given FrameBuffer
        auto item = mappedBuffers.find(const_cast<libcamera::FrameBuffer*>(buffer));
        if (item == mappedBuffers.end()) {
            std::cerr << "Mapped buffer not found, cannot display frame" << std::endl;
            return;
        }

        // Retrieve the pre-mapped buffer
        const std::vector<libcamera::Span<uint8_t>> &retrievedBuffers = item->second;
        if (retrievedBuffers.empty() || retrievedBuffers[0].data() == nullptr) {
            std::cerr << "Mapped buffer is empty or data is null, cannot display frame" << std::endl;
            return;
        }

        frameProcessor->processFrame(frame, streamConfig.size.height, streamConfig.size.width,
                                        retrievedBuffers[0].data());
    } catch (const std::exception &e) {
        std::cerr << "Error rendering frame: " << e.what() << std::endl;
    }
}

int* CameraSensor::getDistances() {
    if (!frameProcessor) {
        std::cerr << "FrameProcessor is not initialized." << std::endl;
        return nullptr;
    }

    // Get a thread-safe copy of distances from the FrameProcessor
    int* acquiredDistances = frameProcessor->getDistances();
    if (!acquiredDistances) {
        std::cerr << "Failed to acquire distances from FrameProcessor." << std::endl;
        return nullptr;
    }

    // Create a new copy of the distances to return
    int slices = frameProcessor->getSlices();
    int* distancesCopy = new int[slices];
    std::copy(acquiredDistances, acquiredDistances + slices, distancesCopy);

    delete[] acquiredDistances;
    return distancesCopy;
}
