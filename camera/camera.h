#ifndef _CAMERA_H
#define _CAMERA_H

#include <stdint.h> // uint8_t
#include <stddef.h> // size_t

#ifdef __cplusplus
#include <iostream>
#include <sstream> // std::ostringstream
#include <vector>
#include <libcamera/libcamera.h>
#include <opencv2/opencv.hpp>

using namespace libcamera;

extern "C" {
#endif

void* cameraInit();
int captureFrame(void* cmHandle);
void cameraTerminate(void* cmHandle);

#ifdef __cplusplus
}
#endif

#endif