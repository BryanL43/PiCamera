#ifndef _CAMERA_H
#define _CAMERA_H

#include <stdint.h> // uint8_t, uint_fast32_t
#include <stdlib.h> // EXIT_FAILURE
#include <stddef.h> // size_t

#ifdef __cplusplus
#include "MapBuffer.h"

extern "C" {
#endif

void* cameraInit();
int captureFrame(void* cmHandle);
void cameraTerminate(void* cmHandle);

#ifdef __cplusplus
}
#endif

#endif