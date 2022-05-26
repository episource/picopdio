#pragma once

#include <Arduino.h>
#include <U8g2lib.h>

#ifndef U8X8_WITH_USER_PTR
#error Not defined: U8X8_WITH_USER_PTR
#endif

class DisplayGpioAdapter {
public:
    virtual void begin();
    virtual void pinMode(pin_size_t pinNumber, PinMode pinMode);
    virtual void digitalWrite(pin_size_t pinNumber, PinStatus status);
    virtual PinStatus digitalRead(pin_size_t pinNumber);
    virtual void writeBank(pin_size_t bankNumber, uint8_t status);
    virtual void setBankMode(pin_size_t bankNumber, PinMode pinMode);
    virtual uint8_t readBank(pin_size_t bankNumber);
};
inline DisplayGpioAdapter DefaultGpioAdapter;

// bufrows:
// - 0 means full buffer (u8g2 conventional suffix _f)
// - 1 means buffer of one page/row (u8g2 conventional suffix _1)
// - 2 means buffer of two pages/rows (u8g2 conventional suffix _2)
template<int bufrows = 1>
class U8G2_ST7565_KS0713_132x48_8080 : public U8G2 {
public:
    U8G2_ST7565_KS0713_132x48_8080(const u8g2_cb_t *rotation, uint8_t dataBank, uint8_t wr_enable, uint8_t rd,
                                   uint8_t cs, uint8_t dc, uint8_t reset, uint8_t power,
                                   DisplayGpioAdapter &displayGpio)
            : U8G2_ST7565_KS0713_132x48_8080(rotation, dataBank, U8X8_PIN_NONE, U8X8_PIN_NONE, U8X8_PIN_NONE,
                                             U8X8_PIN_NONE, U8X8_PIN_NONE, U8X8_PIN_NONE, U8X8_PIN_NONE, U8X8_PIN_NONE,
                                             wr_enable, rd, cs, dc, reset, power, displayGpio) {
    }

    U8G2_ST7565_KS0713_132x48_8080(const u8g2_cb_t *rotation, uint8_t d0, uint8_t d1, uint8_t d2, uint8_t d3,
                                   uint8_t d4, uint8_t d5, uint8_t d6, uint8_t d7, uint8_t wr_enable, uint8_t rd,
                                   uint8_t cs, uint8_t dc, uint8_t reset, uint8_t power,
                                   DisplayGpioAdapter &displayGpio = DefaultGpioAdapter)
            : U8G2_ST7565_KS0713_132x48_8080(rotation, U8X8_PIN_NONE, d0, d1, d2, d3, d4, d5, d6, d7, wr_enable, rd, cs,
                                             dc, reset, power, displayGpio) {
    }

protected:
    U8G2_ST7565_KS0713_132x48_8080(const u8g2_cb_t *rotation, uint8_t dataBank, uint8_t d0, uint8_t d1, uint8_t d2, uint8_t d3,
                                   uint8_t d4, uint8_t d5, uint8_t d6, uint8_t d7, uint8_t wr_enable, uint8_t rd,
                                   uint8_t cs, uint8_t dc, uint8_t reset, uint8_t power,
                                   DisplayGpioAdapter &displayGpio)
            : gpio(displayGpio), powerPin(power), rdPin(rd), dataBank(dataBank) {
        getU8x8()->user_ptr = this;
        u8g2_Setup_ST7565_KS0713_132x48(&u8g2, rotation, byte_cb, gpio_and_delay_cb);
        u8x8_SetPin_8Bit_8080(getU8x8(), d0, d1, d2, d3, d4, d5, d6, d7, wr_enable, cs, dc, reset);
    }

private:
    void u8g2_Setup_ST7565_KS0713_132x48(u8g2_t *u8g2, const u8g2_cb_t *rotation, u8x8_msg_cb byte_cb,
                                         u8x8_msg_cb gpio_and_delay_cb) {
        u8g2_SetupDisplay(u8g2, u8x8_d_ST7565_KS0713_132x48, u8x8_cad_001, byte_cb, gpio_and_delay_cb);
        u8g2_SetupBuffer(u8g2, tileBuf, tileBufHeightInTiles, u8g2_ll_hvline_vertical_top_lsb, rotation);
    }

    static uint8_t u8x8_d_ST7565_KS0713_132x48(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
        if (msg == U8X8_MSG_DISPLAY_SETUP_MEMORY) {
            u8x8_d_helper_display_setup_memory(u8x8, &u8x8_ST7565_KS0713_132x48_display_info);
            return true;
        }
        if (msg == U8X8_MSG_DISPLAY_INIT) {
            const U8G2_ST7565_KS0713_132x48_8080<bufrows> *self = (U8G2_ST7565_KS0713_132x48_8080<bufrows> *) u8x8->user_ptr;

            if (self->rdPin != U8X8_PIN_NONE) {
                self->gpio.digitalWrite(self->rdPin, PinStatus::LOW); // active high; never read => LOW
            }

            if (self->powerPin != U8X8_PIN_NONE) {
                self->gpio.digitalWrite(self->powerPin, PinStatus::LOW); // active low
            }

            u8x8_d_helper_display_init(u8x8);
            u8x8_cad_SendSequence(u8x8, u8x8_d_ST7565_KS0713_132x48_init_seq);
            return true;
        }

        return u8x8_d_st7565_lm6059(u8x8, msg, arg_int, arg_ptr);
    }

    static uint8_t byte_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
        const U8G2_ST7565_KS0713_132x48_8080<bufrows> *self = (U8G2_ST7565_KS0713_132x48_8080<bufrows> *) u8x8->user_ptr;

        if (msg == U8X8_MSG_BYTE_SEND && self->dataBank != U8X8_PIN_NONE) {
            uint8_t *data = (uint8_t *) arg_ptr;
            while (arg_int > 0) {
                self->gpio.writeBank(self->dataBank, *data);
                data++;
                arg_int--;

                u8x8_gpio_Delay(u8x8, U8X8_MSG_DELAY_NANO, u8x8->display_info->data_setup_time_ns);
                u8x8_gpio_call(u8x8, U8X8_MSG_GPIO_E, 0);
                u8x8_gpio_Delay(u8x8, U8X8_MSG_DELAY_NANO, u8x8->display_info->write_pulse_width_ns);
                u8x8_gpio_call(u8x8, U8X8_MSG_GPIO_E, 1);
            }

            return true;
        }

        return u8x8_byte_arduino_8bit_8080mode(u8x8, msg, arg_int, arg_ptr);
    }

    static uint8_t gpio_and_delay_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
        U8G2_ST7565_KS0713_132x48_8080<bufrows> *self = (U8G2_ST7565_KS0713_132x48_8080<bufrows> *) u8x8->user_ptr;
        uint8_t i;
        switch (msg) {
            case U8X8_MSG_GPIO_AND_DELAY_INIT:

                if (self->dataBank != U8X8_PIN_NONE) {
                    self->gpio.setBankMode(self->dataBank, PinMode::OUTPUT);
                }

                if (self->rdPin != U8X8_PIN_NONE) {
                    self->gpio.pinMode(self->rdPin, PinMode::OUTPUT);
                }

                if (self->powerPin != U8X8_PIN_NONE) {
                    self->gpio.pinMode(self->powerPin, PinMode::OUTPUT);
                }

                for (i = 0; i < U8X8_PIN_CNT; i++)
                    if (u8x8->pins[i] != U8X8_PIN_NONE) {
                        if (i < U8X8_PIN_OUTPUT_CNT) {
                            self->gpio.pinMode(u8x8->pins[i], PinMode::OUTPUT);
                        } else {
#ifdef INPUT_PULLUP
                            self->gpio.pinMode(u8x8->pins[i], PinMode::INPUT_PULLUP);
#else
                            self->gpio.pinMode(u8x8->pins[i], PinMode::OUTPUT);
                            self->gpio.digitalWrite(u8x8->pins[i], PinStatus::HIGH);
#endif
                        }
                    }

                break;

            case U8X8_MSG_GPIO_I2C_CLOCK:
            case U8X8_MSG_GPIO_I2C_DATA:
                if (arg_int == 0) {
                    self->gpio.pinMode(u8x8_GetPinValue(u8x8, msg), PinMode::OUTPUT);
                    self->gpio.digitalWrite(u8x8_GetPinValue(u8x8, msg), LOW);
                } else {
#ifdef INPUT_PULLUP
                    self->gpio.pinMode(u8x8_GetPinValue(u8x8, msg), PinMode::INPUT_PULLUP);
#else
                    self->gpio.pinMode(u8x8_GetPinValue(u8x8, msg), PinMode::OUTPUT);
                    self->gpio.digitalWrite(u8x8_GetPinValue(u8x8, msg), PinStatus::HIGH);
#endif
                }
                break;
            default:
                if (msg >= U8X8_MSG_GPIO(0)) {
                    i = u8x8_GetPinValue(u8x8, msg);
                    if (i != U8X8_PIN_NONE) {
                        if (u8x8_GetPinIndex(u8x8, msg) < U8X8_PIN_OUTPUT_CNT) {
                            self->gpio.digitalWrite(i, (PinStatus) arg_int);
                        } else {
                            if (u8x8_GetPinIndex(u8x8, msg) == U8X8_PIN_OUTPUT_CNT) {
                                // call yield() for the first pin only, u8x8 will always request all the pins, so this should be ok
                                yield();
                            }
                            u8x8_SetGPIOResult(u8x8, self->gpio.digitalRead(i) == PinStatus::LOW ? 0 : 1);
                        }
                    }
                    break;
                }

                return u8x8_gpio_and_delay_arduino(u8x8, msg, arg_int, arg_ptr);
        }
        return 1;
    }

    // based on sequence u8x8_d_st7565_lm6059 with following modifications:
    //  - start line set to zero (0x040), was 32 (0x060)
    //  - V0 voltage resistor ratio changed to 5.29 (0x26) as per reciva sources (reciva_lcd_tm13264cbcg.c)
    //  - contrast initialized as 0x06 (was 0x16)
    //  - booster ratio instruction omitted (not supported by KS0713)
    static constexpr uint8_t u8x8_d_ST7565_KS0713_132x48_init_seq[] = {

            U8X8_START_TRANSFER(),                /* enable chip, delay is part of the transfer start */

            U8X8_C(0x0e2),                        /* soft reset */
            U8X8_C(0x0ae),                        /* display off */
            U8X8_C(0x040),                        /* set display start line to 0 */

            U8X8_C(0x0a0),                        /* ADC set to reverse */
            U8X8_C(0x0c8),                        /* common output mode */
            //U8X8_C(0x0a1),		                /* ADC set to reverse */
            //U8X8_C(0x0c0),		                /* common output mode */
            // Flipmode
            // U8X8_C(0x0a0),		                /* ADC set to reverse */
            // U8X8_C(0x0c8),		                /* common output mode */

            U8X8_C(0x0a6),                        /* display normal, bit val 0: LCD pixel off. */
            U8X8_C(0x0a3),                        /* LCD bias 1/9 */
            U8X8_C(0x02f),                        /* all power  control circuits on (regulator, booster and follower) */
            U8X8_C(0x026),                        /* set V0 voltage resistor ratio 5.29  */
            U8X8_CA(0x081, 0x006),                /* set contrast / ref voltage, contrast value */

            U8X8_C(0x0ae),                        /* display off */
            U8X8_C(0x0a5),                        /* enter powersafe: all pixel on, issue 142 */

            U8X8_END_TRANSFER(),                /* disable chip */
            U8X8_END()                            /* end of sequence */
    };

    // based on u8x8_st7565_lm6059_display_info with following parameters changed:
    // - tile_width
    // - default_x_offset
    // - flipmode_x_offset
    // - pixel_width
    // - pixel_height
    static constexpr u8x8_display_info_t u8x8_ST7565_KS0713_132x48_display_info =
            {
                    /* chip_enable_level = */ 0,
                    /* chip_disable_level = */ 1,

                    /* post_chip_enable_wait_ns = */ 150,    /* st7565 datasheet, table 26, tcsh */
                    /* pre_chip_disable_wait_ns = */ 50,    /* st7565 datasheet, table 26, tcss */
                    /* reset_pulse_width_ms = */ 1,
                    /* post_reset_wait_ms = */ 1,
                    /* sda_setup_time_ns = */ 50,        /* st7565 datasheet, table 26, tsds */
                    /* sck_pulse_width_ns = */
                                              120,    /* half of cycle time (100ns according to datasheet), AVR: below 70: 8 MHz, >= 70 --> 4MHz clock */
                    /* sck_clock_hz = */
                                              4000000UL,    /* since Arduino 1.6.0, the SPI bus speed in Hz. Should be  1000000000/sck_pulse_width_ns */
                    /* spi_mode = */ 0,        /* active high, rising edge */
                    /* i2c_bus_clock_100kHz = */ 4,
                    /* data_setup_time_ns = */ 40,    /* st7565 datasheet, table 24, tds8 */
                    /* write_pulse_width_ns = */ 80,    /* st7565 datasheet, table 24, tcclw */
                    /* tile_width = */ 17,        /* width of 17*8=132+4 pixel */
                    /* tile_height = */ 6,
                    /* default_x_offset = */ 0,
                    /* flipmode_x_offset = */ 0,
                    /* pixel_width = */ 132,
                    /* pixel_height = */ 48
            };

    constexpr static uint8_t tileBufHeightInTiles =
            bufrows == 0 ? u8x8_ST7565_KS0713_132x48_display_info.tile_height : bufrows;
    uint8_t tileBuf[tileBufHeightInTiles * u8x8_ST7565_KS0713_132x48_display_info.tile_width * 8];

    DisplayGpioAdapter &gpio;
    uint8_t powerPin;
    uint8_t rdPin;
    uint8_t dataBank;
};