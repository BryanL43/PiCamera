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

        // Directly create an OpenCV Mat from the mapped buffer
        frame = cv::Mat(streamConfig.size.height, streamConfig.size.width, CV_8UC4,
                        const_cast<uint8_t*>(retrievedBuffers[0].data()));

        // Convert to grayscale
        cv::Mat gray;
        cv::cvtColor(frame, gray, cv::COLOR_BGRA2GRAY);

        // Preprocess the grayscale image: Gaussian blur to reduce noise
        cv::GaussianBlur(gray, gray, cv::Size(5, 5), 0);

        // Calculate a dynamic threshold based on the mean intensity of the image.
        // This helps reduce sensitivity to shadows. Larger threshold = less sensitive.
        double meanIntensity = cv::mean(gray)[0];
        int thresholdValue = static_cast<int>(meanIntensity * 0.95);
        if (thresholdValue < 90) thresholdValue = 90;
        if (thresholdValue > 170) thresholdValue = 170;

        // Apply threshold to the image
        cv::Mat thresh;
        cv::threshold(gray, thresh, thresholdValue, 255, cv::THRESH_BINARY_INV);

        // Apply morphological closing to clean up noise and fill small gaps
        cv::morphologyEx(thresh, thresh, cv::MORPH_CLOSE, cv::Mat(), cv::Point(-1, -1), 2);

        // Find contours
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(thresh, contours, cv::RETR_TREE, cv::CHAIN_APPROX_SIMPLE);

        // Store the main contour and its center
        static std::vector<cv::Point> prevMainContour;
        static int prevContourCenterX = 0;
        std::vector<cv::Point> mainContour;
        int contourCenterX = 0;

        // Identify the largest contour by area
        if (!contours.empty()) {
            mainContour = *std::max_element(contours.begin(), contours.end(),
                [](const std::vector<cv::Point> &a, const std::vector<cv::Point> &b) {
                    return cv::contourArea(a) < cv::contourArea(b);
                }
            );

            // Compute contour center using moments
            cv::Moments M = cv::moments(mainContour);
            if (M.m00 != 0) {
                contourCenterX = static_cast<int>(M.m10 / M.m00);
                int contourCenterY = static_cast<int>(M.m01 / M.m00);

                // Correct contour if needed
                if (std::abs(prevContourCenterX - contourCenterX) > 5) {
                    for (const auto &contour : contours) {
                        cv::Moments tmpM = cv::moments(contour);
                        if (tmpM.m00 != 0) {
                            int tmpContourCenterX = static_cast<int>(tmpM.m10 / tmpM.m00);
                            if (std::abs(tmpContourCenterX - prevContourCenterX) < 5) {
                                mainContour = contour;
                                contourCenterX = tmpContourCenterX;
                                break;
                            }
                        }
                    }
                }

                // Update previous contour values
                prevMainContour = mainContour;
                prevContourCenterX = contourCenterX;

                // Calculate the middle of the frame
                int middleX = frame.cols / 2;
                int middleY = frame.rows / 2;

                // Draw the contour and center points
                cv::drawContours(frame, std::vector<std::vector<cv::Point>>{mainContour}, -1, cv::Scalar(0, 255, 0), 3); // Green contour
                cv::circle(frame, cv::Point(contourCenterX, middleY), 7, cv::Scalar(255, 255, 255), -1); // White center dot
                cv::circle(frame, cv::Point(middleX, middleY), 3, cv::Scalar(0, 0, 255), -1); // Red dot for camera center

                // Display the distance and extent
                std::string distanceText = std::to_string(middleX - contourCenterX);
                double extent = cv::contourArea(mainContour) / (cv::boundingRect(mainContour).area());
                std::string extentText = "Weight: " + std::to_string(extent);

                cv::putText(frame, distanceText, cv::Point(contourCenterX + 20, middleY),
                            cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(200, 0, 200), 2);
                cv::putText(frame, extentText, cv::Point(contourCenterX + 20, middleY + 35),
                            cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(200, 0, 200), 1);
            }
        }

        cv::imshow("Camera Feed", frame);
        cv::waitKey(1);

    } catch (const std::exception &e) {
        std::cerr << "Error rendering frame: " << e.what() << std::endl;
    }
}