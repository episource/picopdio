#include "WM8711.h"

#define WM8711_RESET_TIME 100

#define WM8711_LOUT1V   0x02 // shifted: 0x04
#define WM8711_ROUT1V   0x03 // shifted: 0x06
#define WM8711_APANA    0x04 // shifted: 0x08
#define WM8711_APDIGI   0x05 // shifted: 0x0a
#define WM8711_PWR      0x06 // shifted: 0x0c
#define WM8711_IFACE    0x07 // shifted: 0x0e
#define WM8711_SRATE    0x08 // shifted: 0x10
#define WM8711_ACTIVE   0x09 // shifted: 0x12
#define WM8711_RESET    0x0f // shifted: 0x1e

#define WM8711_VOL_ZEROCROSS  (1<<7)
#define WM8711_VOL_BOTH       (1<<8)

#define WM8711_APANA_DAC (1<<4)
#define WM8711_APDIGI_DE_NONE_UNMUTE (0<<1)
#define WM8711_APDIGI_DE_NONE_MUTE (1<<3)

#define WM8711_IFACE_I2S        (2<<0)
#define WM8711_IFACE_16BITS     (0<<2)

void WM8711::begin() {
    writeRegister(WM8711_RESET, 0x0);
    delay(WM8711_RESET_TIME);

    writeRegister(WM8711_LOUT1V, WM8711_VOL_ZEROCROSS | WM8711_VOL_BOTH | 0x00);
    writeRegister(WM8711_ROUT1V, WM8711_VOL_ZEROCROSS | WM8711_VOL_BOTH | 0x00);
    writeRegister(WM8711_APANA, WM8711_APANA_DAC);
    writeRegister(WM8711_APDIGI, WM8711_APDIGI_DE_NONE_MUTE);
    writeRegister(WM8711_PWR, 0x67); // 0x67 -> power on: DACPD, OUTPD, POWER; power off OSCPD, CLKOUTPD
    writeRegister(WM8711_IFACE, WM8711_IFACE_I2S | WM8711_IFACE_16BITS);
    writeRegister(WM8711_SRATE, 0x00); // 0x00 = 48kHZ Sampling Rate @ 12.288MHz MCLK
    writeRegister(WM8711_ACTIVE, 0x00); // activated on unmute
}

void WM8711::writeRegister(uint8_t registerAddress, int val) {
    uint8_t byte1 = (registerAddress & 0x7F) << 1;
    byte1 |= val & 0x100;
    uint8_t  byte2 = val & 0xff;

    _i2c.beginTransmission(_i2cAddress);
    _i2c.write(byte1);
    _i2c.write(byte2);

    uint8_t i2cError = _i2c.endTransmission();
    if (i2cError != 0) {
        DEBUGV("WM8711 i2c error: %d\n", i2cError);
    }
}

void WM8711::mute() {
    writeRegister(WM8711_ACTIVE, 0x00);
    writeRegister(WM8711_APDIGI, WM8711_APDIGI_DE_NONE_MUTE);
}

void WM8711::unmute() {
    writeRegister(WM8711_APDIGI, WM8711_APDIGI_DE_NONE_UNMUTE);
    writeRegister(WM8711_ACTIVE, 0x01);
}

void WM8711::setHeadphoneVolume(uint8_t volume) {
    uint8_t rawVolume = 0;
    if (volume > 0) {
        // 0x00..0x7F; <= 2F is mute
        rawVolume = std::max(0x7f, 0x2f + ((int)volume * 80 / 100));
    }

    writeRegister(WM8711_LOUT1V, WM8711_VOL_ZEROCROSS | WM8711_VOL_BOTH | rawVolume);
}
