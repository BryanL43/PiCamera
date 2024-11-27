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
}

CameraSensor::~CameraSensor() {
    camera->stop();
    camera->release();
    camera.reset();
    cameraManager->stop();
    cv::destroyWindow("Camera Feed");
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
        auto item = mappedBuffers.find(const_cast<libcamera::FrameBuffer *>(buffer));
        if (item == mappedBuffers.end()) {
            std::cerr << "Mapped buffer not found, cannot display frame" << std::endl;
            return;
        }

        // Retrieve the mapped buffer
        const std::vector<libcamera::Span<uint8_t>> &retrievedBuffers = item->second;
        if (retrievedBuffers.empty() || retrievedBuffers[0].data() == nullptr) {
            std::cerr << "Mapped buffer is empty or data is null, cannot display frame" << std::endl;
            return;
        }

        // Directly create an OpenCV Mat from the mapped buffer
        frame = cv::Mat(streamConfig.size.height, streamConfig.size.width, CV_8UC4,
                        const_cast<uint8_t *>(retrievedBuffers[0].data()));

        // Convert to grayscale
        cv::Mat gray;
        cv::cvtColor(frame, gray, cv::COLOR_BGRA2GRAY);

        // Apply thresholding to isolate black regions
        cv::Mat blackline;
        cv::inRange(gray, cv::Scalar(0), cv::Scalar(75), blackline);

        // Perform morphological operations to clean up the binary image
        cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(5, 5));
        cv::morphologyEx(blackline, blackline, cv::MORPH_CLOSE, kernel);

        // Find contours of the black regions
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(blackline, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

        // Center of the frame (camera center)
        int frameCenterX = frame.cols / 2;

        // Variables to track the closest horizontal segment
        double closestDistance = std::numeric_limits<double>::max();
        cv::Vec4i bestLine;

        // Use Hough Line Transform for precise horizontal segment detection
        std::vector<cv::Vec4i> lines;
        cv::HoughLinesP(blackline, lines, 1, CV_PI / 180, 50, 50, 10);

        // Find the line closest to the frame's center
        for (const auto &line : lines) {
            int x1 = line[0], y1 = line[1], x2 = line[2], y2 = line[3];

            // Skip nearly vertical lines
            if (std::abs(y2 - y1) < std::abs(x2 - x1)) {
                int lineCenterX = (x1 + x2) / 2;
                double distanceToCenter = std::abs(lineCenterX - frameCenterX);

                if (distanceToCenter < closestDistance) {
                    closestDistance = distanceToCenter;
                    bestLine = line;
                }
            }
        }

        // Draw the best detected horizontal line and its center
        if (closestDistance < std::numeric_limits<double>::max()) {
            int x1 = bestLine[0], y1 = bestLine[1];
            int x2 = bestLine[2], y2 = bestLine[3];

            // Draw the red line spanning the horizontal width
            cv::line(frame, cv::Point(x1, y1), cv::Point(x2, y2), cv::Scalar(0, 0, 255), 3);

            // Compute and draw the green dot at the center
            int centerX = (x1 + x2) / 2;
            int centerY = (y1 + y2) / 2;
            cv::circle(frame, cv::Point(centerX, centerY), 5, cv::Scalar(0, 255, 0), -1);
        }

        // Display the processed frame
        cv::imshow("Frame with Detected Line and Center", frame);
        cv::waitKey(1);
    } catch (const std::exception &e) {
        std::cerr << "Error during rendering: " << e.what() << std::endl;
    }
}
