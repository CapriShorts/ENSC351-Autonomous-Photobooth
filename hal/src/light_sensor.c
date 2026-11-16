#include "hal/light_sensor.h"
#include "hal/adc.h"

#include <stdint.h>

#define LIGHT_SENSOR_CHANNEL 0
#define ADC_REFERENCE_VOLTAGE 3.3
#define ADC_MAX_VALUE 4095.0

int LightSensor_init(void)
{
    return ADC_init();
}

double LightSensor_readVoltage(void)
{
    uint16_t rawValue;

    if (ADC_readChannel(LIGHT_SENSOR_CHANNEL, &rawValue) != 0) {
        return -1.0;
    }

    return (rawValue / ADC_MAX_VALUE) * ADC_REFERENCE_VOLTAGE;
}

void LightSensor_cleanup(void)
{
    ADC_cleanup();
}
