#ifndef _CAMERA_H
#define _CAMERA_H

#include <stdint.h> // uint8_t, uint_fast32_t
#include <stdlib.h> // EXIT_FAILURE
#include <stddef.h> // size_t

#ifdef __cplusplus
extern "C" {
#endif

typedef void CameraHandle; // Intermediate for C compatibility

CameraHandle* cameraInit(); // void indicate fatal error
void runCamera(CameraHandle* handle);
int* getLineDistances(CameraHandle* handle);
void cameraTerminate(CameraHandle* handle);

#ifdef __cplusplus
}
#endif

#endif