#include "hal/adc.h"

#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define SPI_DEVICE "/dev/spidev0.0"
#define SPI_SPEED_HZ 1000000

static int spiFd = -1;

int ADC_init(void)
{
    spiFd = open(SPI_DEVICE, O_RDWR);
    if (spiFd < 0) {
        perror("Failed to open SPI device");
        return -1;
    }

    uint8_t mode = SPI_MODE_0;
    uint8_t bits = 8;
    uint32_t speed = SPI_SPEED_HZ;

    if (ioctl(spiFd, SPI_IOC_WR_MODE, &mode) < 0 ||
        ioctl(spiFd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0 ||
        ioctl(spiFd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) {
        perror("Failed to configure SPI");
        ADC_cleanup();
        return -1;
    }

    return 0;
}

int ADC_readChannel(uint8_t channel, uint16_t *value)
{
    if (spiFd < 0 || channel > 7 || value == NULL) {
        return -1;
    }

    uint8_t tx[3] = {
        (uint8_t)(0x06 | (channel >> 2)),
        (uint8_t)((channel & 0x03) << 6),
        0x00
    };

    uint8_t rx[3] = {0};

    struct spi_ioc_transfer transfer = {
        .tx_buf = (unsigned long)tx,
        .rx_buf = (unsigned long)rx,
        .len = 3,
        .speed_hz = SPI_SPEED_HZ,
        .bits_per_word = 8
    };

    if (ioctl(spiFd, SPI_IOC_MESSAGE(1), &transfer) < 0) {
        perror("SPI transfer failed");
        return -1;
    }

    *value = (uint16_t)(((rx[1] & 0x0F) << 8) | rx[2]);

    return 0;
}

void ADC_cleanup(void)
{
    if (spiFd >= 0) {
        close(spiFd);
        spiFd = -1;
    }
}
