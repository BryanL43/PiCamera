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

    // Buffer for capturing a frame (JPEG format)
    size_t bufferSize = 1024 * 1024; // 1 MB buffer size
    uint8_t buffer[bufferSize];

    // Capture a frame and store it in the buffer
    int result = captureFrame(cameraHandle, buffer, bufferSize);
    if (result != 0) {
        fprintf(stderr, "Failed to capture a frame. Error code: %d\n", result);
        cameraTerminate(cameraHandle);
        return -1;
    }
    printf("Frame captured successfully.\n");

    // Save the captured frame as a JPEG file
    // FILE* file = fopen("captured_frame.jpg", "wb");
    // if (file) {
    //     fwrite(buffer, 1, bufferSize, file);
    //     fclose(file);
    //     printf("Image saved to captured_frame.jpg\n");
    // } else {
    //     fprintf(stderr, "Failed to open file for saving image.\n");
    //     return -1;
    // }

    // Terminate the camera
    cameraTerminate(cameraHandle);
    printf("Camera terminated successfully.\n");

    return 0;
}
