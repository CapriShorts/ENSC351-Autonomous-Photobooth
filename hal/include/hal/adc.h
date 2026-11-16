#ifndef ADC_H
#define ADC_H

#include <stdint.h>

// Initialize MCP3208 SPI communication
int ADC_init(void);

// Read a raw 12-bit value from channel 0-7
int ADC_readChannel(uint8_t channel, uint16_t *value);

// Close SPI device
void ADC_cleanup(void);

#endif
