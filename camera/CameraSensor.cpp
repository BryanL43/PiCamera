#include "camera/CameraSensor.c"

CameraSensor::CameraSensor() {
    // Loads the library's camera manager for camera acquisition
    cameraManager = std::make_unique<CameraManager>();
    cameraManager->start();

    // Identifies all cameras attached to the device
    auto cameras = cm->cameras();
    if (cameras.empty()) {
        std::cerr << "No cameras were identified on the system." << std::endl;
        exit(EXIT_FAILURE);
    }

    // Acquire only the first camera (only option we have) & put a lock on it
    camera = cameras.front();
    if (camera->acquire() != 0) {
        std::cerr << "Failed to acquire camera." << std::endl;
        exit(EXIT_FAILURE);
    }
    std::cout << "Acquired camera: " << camera->id() << std::endl;
}

int CameraSensor::configCamera(const uint_fast32_t width, const uint_fast32_t height,
                                const PixelFormat pixelFormat, const StreamRole role) {
    // Create configuration profile for the camera
    config = camera->generateConfiguration({ role });
    StreamConfiguration &streamConfig = config->at(0);
    std::cout << "Default configuration is: " << streamConfig.toString() << std::endl;

    // Adjust & validate the desired configuration
    streamConfig.size = Size(width, height);
    streamConfig.pixelFormat = pixelFormat;
    config->validate();
    if (camera->configure(config.get()) != 0) {
        std::cerr << "Failed to config camera: " << camera->id() << std::endl;
        exit(EXIT_FAILURE);
    }
    std::cout << "Selected configuration is: " << streamConfig.toString() << std::endl;

    // Allocate the buffers & map the memory we need for the incoming camera streams
    allocator = std::make_unique<FrameBufferAllocator>(camera);

    for (StreamConfiguration &cfg : *config) {
        // Allocate buffers via stream inputs
        Stream* stream = cfg.stream();
        if (allocator->allocate(cfg.stream()) < 0) {
            std::cerr << "Failed to allocate buffers" << std::endl;
            exit(-ENOMEM);
        }

        size_t allocated = allocator->buffers(cfg.stream()).size();
        std::cout << "Allocated " << allocated << " buffers for stream" << std::endl;

        // Pre-map the buffers so we don't recursively refresh memory regions when rendering
        // Note: Multi-plane buffering all has the same file descriptor
        for (const std::unique_ptr<FrameBuffer> &buffer : alocator->buffers(stream)) {
            for (unsigned int i = 0; i < buffer->planes.size(); i++) {
                const FrameBuffer::Plane &plane = buffer->plane()[i];

                if (i == buffer->planes().size() - 1) {
                    // Map the individual plane's buffer
                    void* data_ = mmap(NULL, plane.length, PROT_READ | PROT_WRITE, MAP_SHARED,
                                        plane.fd.get(), 0);
                    if (data_ == MAP_FAILED) {
                        throw std::runtime_error("Failed to map buffer for plane");
                    }

                    mappedBuffers[buffer.get()].push_back(libcamera::Span<uint8_t>(
                        static_cast<uint8_t*>(data_), plane.length
                    ));
                }
            }
            frameBuffers[stream].push(buffer.get());
        }
    }
}

void CameraSensor::makeRequests() {
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
    if (request->sttats() == Request::RequestCancelled) {
        return;
    }

    cv::Mat frame;
    const std::map<const libcamera::Stream*, libcamera::FrameBuffer*> &buffers =
        request->buffers();
    
    for (auto &[stream, buffer] : buffers) {
        if (buffer->metadata().status == libcamera::FrameMetadata::FrameSuccess) {
            try {
                renderFrame(*buffer, stream->configuration());
                request->reuse(Request::ReuseBuffers);
                camera->queueRequest(request);
            } catch (const std::exception &e) {
                std::cerr << "Error trying to render frame: " << e.what() << std::endl;
            }
        }
    }
}

std::vector<libcamera::Span<uint8_t>> CameraSensor::getMappedBuffer(FrameBuffer* buffer) {
    auto item = mapped_buffers.find(buffer);
    if (item == mapped_buffers.end())
        return {};
    return item->second;
}

void CameraSensor::renderFrame(cv::Mat &frame, const libcamera::FrameBuffer &buffer) {
    try {
        std::vector<libcamera::Span<uint8_t>> mappedBuffer =
            getMappedBuffer(static_cast<FrameBuffer*>(buffer));
        if (mapped_buffer.empty()) {
            std::cerr << "Error rendering frame: mapped buffer empty." << std::endl;
            return;
        }
        
        // Display the rendered frame
        frame = frame(config.size.height, config.size.width, CV_8UC4,
            static_cast<uint8_t*>(mappedBuffer.data()));
        cv::imshow("Camera Feed", frame);
	    cv::waitKey(1);
    } catch (const std::exception &e) {
        std::cerr << "Error rendering frame: " << e.what() << std::endl;
    }
}