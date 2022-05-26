#pragma once

#include <Arduino.h>

class RotaryDecoder {
public:
    enum Step : int8_t {
        NO_STEP = 0,
        FORWARD = 1,
        BACKWARD = -1
    };

    Step decode(bool highPinVal, bool lowPinVal);
    Step decode(uint8_t highPinVal, uint8_t lowPinVal);
    Step decode(PinStatus highPinVal, PinStatus lowPinVal);
    Step readAndDecode(uint8_t highPin, uint8_t lowPin);

private:
    enum State : uint8_t;
    static const State TRANSITION_LUT[14][4];

    State _s;
};
