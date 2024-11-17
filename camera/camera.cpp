#include "camera.h"

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
int captureFrame(void* cmHandle, uint8_t* buffer, size_t bufferSize) {
    // Acquire & validate CameraManager from C to C++ conversion
    auto* cm = static_cast<CameraManager*>(cmHandle);
    if (cm == nullptr) {
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
        camera->generateConfiguration({StreamRole::VideoRecording});
    if (!config) {
        std::cerr << "Failed to create config profile for camera: " << camera->id() << std::endl;
        return -4;
    }
    StreamConfiguration &streamConfig = config->at(0);
    std::cout << "Default configuration is: "<< streamConfig.toString() << std::endl;

    // Adjust & validate the VideoRecording resolution
    config->at(0).size = libcamera::Size(640, 480);
    config->validate();
    std::cout << "Validated configuration is: " << streamConfig.toString() << std::endl;
    if (camera->configure(config.get()) != 0) {
        std::cerr << "Failed to reconfig camera: " << camera->id() << std::endl;
        return -5;
    }

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