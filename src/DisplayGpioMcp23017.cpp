#include "DisplayGpioMcp23017.h"

PlatformAndSharedMCP23017Gpio::PlatformAndSharedMCP23017Gpio(MCP23017 &mcp23017) : mcp23017(mcp23017){

}

void PlatformAndSharedMCP23017Gpio::pinMode(pin_size_t pinNumber, PinMode pinMode) {
    pin_size_t internalPinNumber = pinNumber;
    if (translatePin(pinNumber, internalPinNumber)) {
        mcp23017.pinMode(internalPinNumber, pinMode);
        DEBUGMCP_ERR(mcp23017, "MCP23017 pinMode failed: 0x%x\n");
    } else {
        DisplayGpioAdapter::pinMode(pinNumber, pinMode);
    }
}

void PlatformAndSharedMCP23017Gpio::digitalWrite(pin_size_t pinNumber, PinStatus status) {
    pin_size_t internalPinNumber = pinNumber;
    if (translatePin(pinNumber, internalPinNumber)) {
        mcp23017.digitalWrite(internalPinNumber, status);
        DEBUGMCP_ERR(mcp23017, "MCP23017 digitalWrite failed: 0x%x\n");
    } else {
        DisplayGpioAdapter::digitalWrite(pinNumber, status);
    }
}

PinStatus PlatformAndSharedMCP23017Gpio::digitalRead(pin_size_t pinNumber) {
    pin_size_t internalPinNumber = pinNumber;
    if (translatePin(pinNumber, internalPinNumber)) {
        PinStatus result = (PinStatus)mcp23017.digitalRead(internalPinNumber);
        DEBUGMCP_ERR(mcp23017, "MCP23017 digitalRead failed: 0x%x\n");
        return result;
    }
    return DisplayGpioAdapter::digitalRead(pinNumber);
}

void PlatformAndSharedMCP23017Gpio::writeBank(pin_size_t bankNumber, uint8_t status) {
    pin_size_t internalBankNumber = bankNumber;
    if (translatePin(bankNumber, internalBankNumber)) {
        mcp23017.write8(internalBankNumber, status);
        DEBUGMCP_ERR(mcp23017, "MCP23017 write8 failed: 0x%x\n");
    }
    DisplayGpioAdapter::writeBank(bankNumber, status);
}

void PlatformAndSharedMCP23017Gpio::setBankMode(pin_size_t bankNumber, PinMode pinMode) {
    pin_size_t internalBankNumber = bankNumber;
    if (translatePin(bankNumber, internalBankNumber)) {
        mcp23017.pinMode8(internalBankNumber, pinMode == OUTPUT ? 0 : 1);
        DEBUGMCP_ERR(mcp23017, "MCP23017 pinMode8 failed: 0x%x\n");
        mcp23017.setPullup8(internalBankNumber, pinMode == INPUT_PULLUP ? 1 : 0);
        DEBUGMCP_ERR(mcp23017, "MCP23017 setPullup8 failed: 0x%x\n");
    }
    DisplayGpioAdapter::setBankMode(bankNumber, pinMode);
}

uint8_t PlatformAndSharedMCP23017Gpio::readBank(pin_size_t bankNumber) {
    pin_size_t internalBankNumber = bankNumber;
    if (translatePin(bankNumber, internalBankNumber)) {
        uint8_t result = mcp23017.read8(internalBankNumber);
        DEBUGMCP_ERR(mcp23017, "MCP23017 read8 failed: 0x%x\n");
        return result;
    }
    return DisplayGpioAdapter::readBank(bankNumber);
}

bool PlatformAndSharedMCP23017Gpio::translatePin(pin_size_t externalPinNumber, pin_size_t &internalPinNumber) const {
    if (externalPinNumber >= mcpGpioOffset) {
        internalPinNumber = externalPinNumber - mcpGpioOffset;
        return true;
    }

    internalPinNumber = externalPinNumber;
    return false;
}


ExclusiveMCP23017Gpio::ExclusiveMCP23017Gpio(int mcp23017Addr, TwoWire *i2c)
    : mcp23017(mcp23017Addr, i2c) {
}

void ExclusiveMCP23017Gpio::begin() {
    for(int i = 0; !mcp23017.begin(); ++i) {
        DEBUGMCP_ERR(mcp23017, "MCP23017 begin failed: 0x%x\n");
        delay(10);

        if (i > 10) {
            DEBUGV("MCP23017 persistent communication error - rebooting!");
            reset_request = true;
            return;
        }
    }

    reset_request = false;
}

void ExclusiveMCP23017Gpio::pinMode(pin_size_t pinNumber, PinMode pinMode) {
    mcp23017.pinMode(pinNumber, pinMode);
    HANDLEMCP_ERR(mcp23017, "MCP23017 pinMode failed: 0x%x\n");
}

void ExclusiveMCP23017Gpio::digitalWrite(pin_size_t pinNumber, PinStatus status) {
    mcp23017.digitalWrite(pinNumber, status);
    HANDLEMCP_ERR(mcp23017, "MCP23017 digitalWrite failed: 0x%x\n");
}

PinStatus ExclusiveMCP23017Gpio::digitalRead(pin_size_t pinNumber) {
    pin_size_t bankNumber = pinNumber < 8 ? 0 : 1;
    uint8_t pBit = pinNumber < 8 ? pinNumber : pinNumber - 8;
    uint8_t status = readBank(bankNumber);

    return (PinStatus)((status >> pBit) & 0x1);
}

void ExclusiveMCP23017Gpio::writeBank(pin_size_t bankNumber, uint8_t status) {
    if (bankNumber > MAX_BANK) {
        DEBUGMCP("writeBank failed - illegal bank number: %d\n", bankNumber);
    }

    uint8_t curStatus = gpioState[bankNumber];

    if (curStatus != status) {
        mcp23017.write8(bankNumber, status);
        HANDLEMCP_ERR(mcp23017, "MCP23017 write8 failed: 0x%x\n");

        if (mcp23017.lastError() == MCP23017_OK) {
            gpioState[bankNumber] = status;
        }
    }
}

void ExclusiveMCP23017Gpio::setBankMode(pin_size_t bankNumber, PinMode pinMode) {
    mcp23017.pinMode8(bankNumber, pinMode == OUTPUT ? 0 : 1);
    HANDLEMCP_ERR(mcp23017, "MCP23017 pinMode8 failed: 0x%x\n");
    mcp23017.setPullup8(bankNumber, pinMode == INPUT_PULLUP ? 1 : 0);
    HANDLEMCP_ERR(mcp23017, "MCP23017 setPullup8 failed: 0x%x\n");
}

uint8_t ExclusiveMCP23017Gpio::readBank(pin_size_t bankNumber) {
    if (bankNumber > MAX_BANK) {
        DEBUGMCP("readBank failed - illegal bank number: %d\n", bankNumber);
    }

    uint8_t result = mcp23017.read8(bankNumber);
    HANDLEMCP_ERR(mcp23017, "MCP23017 read8 failed: 0x%x\n");

    if (mcp23017.lastError() == MCP23017_OK) {
        gpioState[bankNumber] = result;
    }

    return result;
}

bool ExclusiveMCP23017Gpio::resetRequest() {
    return reset_request;
}
