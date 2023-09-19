#pragma once

#include <Arduino.h>
#include <MCP23017.h>
#include "DisplayDriver.h"

#if !defined(DEBUG_RP2040_PORT)
#define DEBUGMCP(...) do { } while(0)
#define DEBUGMCP_ERR(...) do { } while(0)
#define HANDLEMCP_ERR(mcp, fmt) do {  int lastError = mcp.lastError(); if (lastError != MCP23017_OK) { reset_request = true; } } while(0)
#else
#define DEBUGMCP(...) DEBUGV(__VA_ARGS__)
#define DEBUGMCP_ERR(mcp, fmt) do {  int lastError = mcp.lastError(); if (lastError != MCP23017_OK) { DEBUGV(fmt, lastError); } } while(0)
#define HANDLEMCP_ERR(mcp, fmt) do {  int lastError = mcp.lastError(); if (lastError != MCP23017_OK) { reset_request = true; DEBUGV(fmt, lastError); } } while(0)
#endif

/// platform gpio (pins < 100), and mcp23017 gpio (pins >= 100, banks 100+101)
/// MCP23017 not owned exclusively
class PlatformAndSharedMCP23017Gpio : public DisplayGpioAdapter {
public:
    PlatformAndSharedMCP23017Gpio(MCP23017 &mcp23017);

    virtual void pinMode(pin_size_t pinNumber, PinMode pinMode) override;
    virtual void digitalWrite(pin_size_t pinNumber, PinStatus status) override;
    virtual PinStatus digitalRead(pin_size_t pinNumber) override;
    virtual void writeBank(pin_size_t bankNumber, uint8_t status) override;
    virtual void setBankMode(pin_size_t bankNumber, PinMode pinMode) override;
    virtual uint8_t readBank(pin_size_t bankNumber) override;

    constexpr static pin_size_t mcpGpioOffset = 100;
private:
    bool translatePin(pin_size_t externalPinNumber, pin_size_t &internalPinNumber) const;

    MCP23017 &mcp23017;
};

/// Gpio using MCP23017 only; MCP23017 is expected to be used exclusively,
/// that is the MCP23017 will be used for display communication only: this
/// permits internal caching reducing I2C communication overhead
/// Uses MCP23017 pin numbers (0-15) and bank numbers (0-1)
class ExclusiveMCP23017Gpio : public DisplayGpioAdapter {
public:
    ExclusiveMCP23017Gpio(int mcp23017Addr, TwoWire *i2c = &Wire);

    virtual void begin() override;
    virtual void pinMode(pin_size_t pinNumber, PinMode pinMode) override;
    virtual void digitalWrite(pin_size_t pinNumber, PinStatus status) override;
    virtual PinStatus digitalRead(pin_size_t pinNumber) override;
    virtual void writeBank(pin_size_t bankNumber, uint8_t status) override;
    virtual void setBankMode(pin_size_t bankNumber, PinMode pinMode) override;
    virtual uint8_t readBank(pin_size_t bankNumber) override;

    virtual bool resetRequest();
private:
    constexpr static pin_size_t MAX_BANK = 1;

    uint8_t gpioState[2] {};
    MCP23017 mcp23017;
    bool reset_request = false;
};
