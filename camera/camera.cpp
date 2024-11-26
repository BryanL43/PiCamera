#include "camera.h"
#include "CameraSensor.hpp"

CameraHandle* cameraInit() {
    CameraSensor* camera = new CameraSensor();
    
    // Configure camera with desired dimension, color format, & type of stream
    // All pixel format: https://libcamera.org/api-html/formats_8h_source.html
    const uint_fast32_t width = 640;
    const uint_fast32_t height = 480;
    const libcamera::PixelFormat pixelFormat = libcamera::formats::XRGB8888;
    const libcamera::StreamRole role = libcamera::StreamRole::Raw;
    
    int result = camera->configCamera(width, height, pixelFormat, role);
    if (result != 0) {
        delete camera;
        return NULL;
    }

    return reinterpret_cast<CameraHandle*>(camera);
}

void runCamera(CameraHandle* handle) {
    if (!handle) {
        std::cerr << "No camera handle found" << std::endl;
        return;
    }

    CameraSensor* camera = static_cast<CameraSensor*>(handle);
    try {
        std::cout << "Starting camera..." << std::endl;
        camera->startCamera();
        std::this_thread::sleep_for(std::chrono::milliseconds(3000));
        std::cout << "Stopping camera.." << std::endl;
        camera->stopCamera();
    } catch (const std::exception& e) {
        std::cerr << "Camera failed to start: " << e.what() << std::endl;
        return;
    }
}

void cameraTerminate(CameraHandle* handle) {
    if (!handle) {
        std::cerr << "Camera handle is null!" << std::endl;
        return;
    }

    // Cast handle back to CameraSensor
    CameraSensor* camera = reinterpret_cast<CameraSensor*>(handle);
    if (camera == nullptr) {
        std::cerr << "FATAL: Invalid camera pointer!" << std::endl;
        return;
    }

    delete camera;
    camera = nullptr;
    handle = nullptr;
}