#include <stdio.h>
#include "camera.h"  // Include the C interface header

int main() {
    // Initialize the camera
    void* cameraHandle = cameraInit();
    if (cameraHandle == NULL) {
        fprintf(stderr, "Failed to initialize the camera.\n");
        return -1;
    }
    printf("Camera initialized successfully.\n");

    // Capture a live frame
    int result = captureFrame(cameraHandle);
    if (result != 0) {
        fprintf(stderr, "Failed to capture a frame. Error code: %d\n", result);
        cameraTerminate(cameraHandle);
        return -1;
    }
    printf("Frame captured successfully.\n");

    // Terminate the camera
    cameraTerminate(cameraHandle);
    printf("Camera terminated successfully.\n");

    return 0;
}
