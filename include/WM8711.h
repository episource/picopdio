#pragma once

#include <stdint.h>
#include <Wire.h>

/// @brief Wolfson WM8711 control via I2C
class WM8711 final {
public:
    WM8711(TwoWire& i2c, uint8_t i2cAddress) : _i2c(i2c), _i2cAddress(i2cAddress) {}

    void begin();
    void writeRegister(uint8_t registerAddress, int val);

    void mute();
    void unmute();

    void setHeadphoneVolume(uint8_t volume);

private:
    uint8_t _i2cAddress;
    TwoWire& _i2c;
};
