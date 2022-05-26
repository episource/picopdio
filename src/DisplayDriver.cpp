#include "DisplayDriver.h"

void DisplayGpioAdapter::begin() {
    // nothing to do
}

void DisplayGpioAdapter::pinMode(pin_size_t pinNumber, PinMode pinMode) {
    ::pinMode(pinNumber, pinMode);
}

void DisplayGpioAdapter::digitalWrite(pin_size_t pinNumber, PinStatus status) {
    ::digitalWrite(pinNumber, status);
}

PinStatus DisplayGpioAdapter::digitalRead(pin_size_t pinNumber) {
    return ::digitalRead(pinNumber);
}

void DisplayGpioAdapter::writeBank(pin_size_t bankNumber, uint8_t status) {
    DEBUGV("cannot write - illegal bank number: %d\n", bankNumber);
}

void DisplayGpioAdapter::setBankMode(pin_size_t bankNumber, PinMode pinMode) {
    DEBUGV("cannot set mode - illegal bank number: %d\n", bankNumber);
}

uint8_t DisplayGpioAdapter::readBank(pin_size_t bankNumber) {
    DEBUGV("cannot read - illegal bank number: %d\n", bankNumber);
    return 0;
}
