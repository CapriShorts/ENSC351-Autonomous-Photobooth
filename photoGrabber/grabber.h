#ifndef GRABBER_H
#define GRABBER_H

#include <stdbool.h>

// Start an asynchronous single photo capture
void Grabber_capture(void);

// Start an asynchronous burst of photos
void Grabber_captureBurst(int count);

// Check if a photo capture is in progress
bool Grabber_isCapturing(void);

// Wait for any active capture to finish
void Grabber_cleanup(void);

#endif