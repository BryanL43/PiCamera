#ifndef _CAMERASENSOR_H_
#define _CAMERASENSOR_H_

#include <iostream>
#include <queue>
#include <sys/mman.h> // mmap & munmap

#include <libcamera/libcamera.h>
#include <opencv2/opencv.hpp>

#include "FrameProcessor.hpp"

class CameraSensor {
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

    CameraSensor(); // Holds initializating steps for the camera
    ~CameraSensor();

    int configCamera(const uint_fast32_t width, const uint_fast32_t height,
                    const PixelFormat pixelFormat, const StreamRole role);
    void startCamera();

private:
    std::shared_ptr<Camera> camera;
    std::unique_ptr<CameraManager> cameraManager;
    std::unique_ptr<CameraConfiguration> config;
    std::unique_ptr<FrameBufferAllocator> allocator;
    std::vector<std::unique_ptr<Request>> requests;

    // House the Span (mapped memory: 1st param = region offset of file; 2nd param = size)
    std::map<FrameBuffer*, std::vector<libcamera::Span<uint8_t>>> mappedBuffers;

    // Pair that formulate each image
    std::map<Stream*, std::queue<FrameBuffer*>> frameBuffers;

    // Modularize frame processing event
    std::unique_ptr<FrameProcessor> frameProcessor;

    void sendRequests();
    void requestComplete(Request* request);
    void renderFrame(cv::Mat &frame, const libcamera::FrameBuffer *buffer);
};

#endif