#ifndef GRABBER_H
#define GRABBER_H

// Start an asynchronous photo capture
void Grabber_capture(void);

// Wait for any active capture to finish
void Grabber_cleanup(void);

#endif