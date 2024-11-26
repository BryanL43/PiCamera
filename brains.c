#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include "camera/camera.h"

CameraHandle* camera = NULL;

void stopHandler(int signal) {
    if (camera) {
        printf("\nReceived SIGTSTP, terminating the camera...\n");
        cameraTerminate(camera);
        printf("Camera terminated successfully.\n");
    }
    exit(EXIT_SUCCESS);
}

int main(int argc, char* argv[]) {
    signal(SIGINT, stopHandler);

    camera = cameraInit();
    if (!camera) {
        return EXIT_FAILURE;
    }
    printf("Camera initialized successfully.\n");

    runCamera(camera);

    return EXIT_SUCCESS;
}
