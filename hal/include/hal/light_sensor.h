//light sensor.h
#ifndef LIGHT_SENSOR_H
#define LIGHT_SENSOR_H

int LightSensor_init(void);
double LightSensor_readVoltage(void);
void LightSensor_cleanup(void);

#endif