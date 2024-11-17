#ifndef _CAMERA_H
#define _CAMERA_H

#include <stdint.h> // uint8_t
#include <stddef.h> // size_t

#ifdef __cplusplus
#include <iostream>
#include <fstream>
#include <vector>
#include <libcamera/libcamera.h>
#include <opencv2/opencv.hpp>

using namespace libcamera;

extern "C" {
#endif

void* cameraInit();
int captureFrame(void* cmHandle, uint8_t* buffer, size_t bufferSize);
void cameraTerminate(void* cmHandle);

#ifdef __cplusplus
}
#endif

#endif