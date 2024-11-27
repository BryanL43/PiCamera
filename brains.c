#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <pthread.h>
#include <unistd.h> // usleep
#include "camera/camera.h"

CameraHandle* camera = NULL;
volatile short running = 1;

void stopHandler(int signal) {
    printf("\n\nCtrl + C (SIGINT) detected, stopping the camera...\n");
    running = 0;
}

void* cameraThreadRoutine(void* arg) {
    runCamera(camera);
    return NULL;
}

int main(int argc, char* argv[]) {
    signal(SIGINT, stopHandler);

    camera = cameraInit();
    if (!camera) {
        fprintf(stderr, "Failed to initialize the camera.\n");
        return EXIT_FAILURE;
    }
    printf("Camera initialized successfully.\n");

    printf("Starting the camera. Press Ctrl+C to stop.\n");
    pthread_t cameraThread;
    if (pthread_create(&cameraThread, NULL, cameraThreadRoutine, NULL) != 0) {
        fprintf(stderr, "Failed to create the camera thread.\n");
        cameraTerminate(camera);
        return EXIT_FAILURE;
    }

    // Simulate an application event loop or wait for the signal
    while (running) {
        usleep(100000); // 100ms
        printf("Running\n");
    }

    // Clean up
    if (pthread_join(cameraThread, NULL) != 0) {
        fprintf(stderr, "Failed to join the camera thread.\n");
    }
    cameraTerminate(camera);
    printf("Camera terminated successfully.\n");

    return EXIT_SUCCESS;
}
