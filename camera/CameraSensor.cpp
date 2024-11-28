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

        // Create an OpenCV Mat from the mapped buffer
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

        // Define slicing parameters to segment the image & black line
        int slices = 4;
        int sliceHeight = gray.rows / slices;

        std::vector<int> sliceDirections(slices, 0); // Store directions per slice
        std::vector<cv::Point> contourCenters; // To store the centers of the contours
        for (int i = 0; i < slices; ++i) {
            int startY = i * sliceHeight;
            cv::Rect sliceROI(0, startY, gray.cols, sliceHeight);
            cv::Mat slice = gray(sliceROI);

            // Apply threshold & morphological closing to clean up noise and fill small gaps
            cv::Mat thresh;
            cv::threshold(slice, thresh, thresholdValue, 255, cv::THRESH_BINARY_INV);
            cv::morphologyEx(thresh, thresh, cv::MORPH_CLOSE, cv::Mat(), cv::Point(-1, -1), 2);

            // Find contours
            std::vector<std::vector<cv::Point>> contours;
            cv::findContours(thresh, contours, cv::RETR_TREE, cv::CHAIN_APPROX_SIMPLE);

            if (!contours.empty()) {
                // Find the largest contour by area
                auto mainContour = *std::max_element(contours.begin(), contours.end(),
                    [](const std::vector<cv::Point> &a, const std::vector<cv::Point> &b) {
                        return cv::contourArea(a) < cv::contourArea(b);
                    }
                );

                // Compute the segment's contour center
                cv::Moments M = cv::moments(mainContour);
                int contourCenterX = (M.m00 != 0) ? static_cast<int>(M.m10 / M.m00) : 0;
                contourCenters.push_back(cv::Point(contourCenterX, sliceHeight / 2 + startY));

                // Compute direction (middle of the slice - contour center)
                int sliceMiddleX = slice.cols / 2;
                int direction = (sliceMiddleX - contourCenterX) * (cv::contourArea(mainContour) / (cv::boundingRect(mainContour).area()));
                sliceDirections[i] = direction;

                // Draw the Green contour and white center dot (the center of the contour)
                cv::drawContours(frame(sliceROI), std::vector<std::vector<cv::Point>>{mainContour}, -1, cv::Scalar(0, 255, 0), 2);
                cv::circle(frame(sliceROI), cv::Point(contourCenterX, sliceHeight / 2), 5, cv::Scalar(255, 255, 255), -1);

                // Calculate distance and extent
                std::string distanceText = "Dist: " + std::to_string(sliceMiddleX - contourCenterX);
                double extent = cv::contourArea(mainContour) / (cv::boundingRect(mainContour).area());
                std::string extentText = "Weight: " + std::to_string(extent);

                // Display distance and extent
                cv::putText(frame(sliceROI), distanceText, cv::Point(contourCenterX + 20, sliceHeight / 2),
                            cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(200, 0, 200), 2);
                cv::putText(frame(sliceROI), extentText, cv::Point(contourCenterX + 20, sliceHeight / 2 + 35),
                            cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(200, 0, 200), 1);
            }

            // Draw a red dot at the center of each slice for camera center
            int sliceMiddleX = slice.cols / 2;
            int sliceMiddleY = sliceHeight / 2 + startY;
            cv::circle(frame, cv::Point(sliceMiddleX, sliceMiddleY), 5, cv::Scalar(0, 0, 255), -1);

            // Draw a pink horizontal line connecting the white dot to the red dot
            if (!contourCenters.empty()) {
                cv::Point whiteDot = contourCenters.back(); // Last contour center
                cv::line(frame, whiteDot, cv::Point(sliceMiddleX, whiteDot.y), cv::Scalar(255, 20, 147), 2);
            }
        }

        // Draw blue lines connecting each white dot
        for (size_t i = 1; i < contourCenters.size(); ++i) {
            cv::line(frame, contourCenters[i-1], contourCenters[i], cv::Scalar(255, 0, 0), 2, cv::LINE_8, 0);
        }

        // Draw blue line from the first to the last white dot
        if (contourCenters.size() > 1) {
            cv::line(frame, contourCenters.front(), contourCenters.back(), cv::Scalar(255, 0, 0), 2, cv::LINE_8, 0);
        }

        // Display the final result
        cv::imshow("Camera Feed", frame);
        cv::waitKey(1);

    } catch (const std::exception &e) {
        std::cerr << "Error rendering frame: " << e.what() << std::endl;
    }
}
