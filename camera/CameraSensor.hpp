#ifndef _CAMERASENSOR_H_
#define _CAMERASENSOR_H_

#include <iostream>
#include <sstream> // std::ostringstream
#include <queue>
#include <sys/mman.h> // mmap & munmap
#include <thread> // sleep_for
#include <chrono>
#include <libcamera/libcamera.h>
#include <opencv2/opencv.hpp>

class CameraSensor {
private:
    std::shared_ptr<Camera> camera;
    std::unique_ptr<CameraManager> cameraManager;
    std::unique_ptr<CameraConfiguration> config;
    std::unique_ptr<FrameBufferAllocator> allocator;
    std::vector<std::unique_ptr<Request>> requests;
    std::map<Stream*, std::queue<FrameBuffer*>> frameBuffers; // Pair that formulate each image
    std::map<FrameBuffer*, std::vector<libcamera::Span<uint8_t>>> mappedBuffers;

    void makeRequests();
    void requestComplete(Request* request);
    std::vector<libcamera::Span<uint8_t>> getMappedBuffer(FrameBuffer* buffer);

public:
    using Camera = libcamera::Camera;
    using CameraManager = libcamera::CameraManager;
    using CameraConfiguration = libcamera::CameraConfiguration;
    using Stream = libcamera::Stream;
    using StreamRole = libcamera::StreamRole;
    using PixelFormat = libcamera::PixelFormat;
    using StreamConfiguration = libcamera::StreamConfiguration;
    using FrameBuffer = libcamera::FrameBuffer;
    using FrameBufferAllocator = libcamera::FrameBufferAllocator;
    using FrameMetadata = libcamera::FrameMetadata;
    using Request = libcamera::Request;

    CameraSensor(); // Holds initializating steps
    ~CameraSensor();

    void configCamera(const uint_fast32_t width, const uint_fast32_t height,
                    const PixelFormat pixelFormat, const StreamRole role);
    void startCamera();
    void stopCamera();

    void renderFrame(cv::Mat &frame, const libcamera::FrameBuffer &buffer);
}

#endif