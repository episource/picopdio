#pragma once

#include <stdint.h>

#include <CString.h>
#include <pico.h>

#include "DisplayDriver.h"
#include "DisplayGpioMcp23017.h"

#define USER_INTERFACE_FONT  u8g2_font_profont11_mf

class UserInterface final {
public:
    UserInterface(int mcp23017Addr, TwoWire &i2c, uint8_t dataBank, uint8_t wr_enable, uint8_t rd,
                  uint8_t cs, uint8_t dc, uint8_t reset, uint8_t power, int titlePageChangeIntervalMs = 4000)
                  : _display8080Gpio(mcp23017Addr, &i2c),
                    _u8g2(U8G2_R0, dataBank, wr_enable, rd, cs, dc, reset, power, _display8080Gpio),
                    _titlePageChangeIntervalMs(titlePageChangeIntervalMs) {
        mutex_init(&_sharedStateLock);
    }

    // call these from same core only
    void begin(bool showInitMessage = false);
    bool loop();

    // methods below may be called from any core
    void showLoadScreen();
    void showLoadScreen(uint8_t stationNumber, char* stationName);
    void showTitleScreen();
    void showTitleScreen(char* title, uint8_t bufferLevel);
    void setTitle(char* title);
    void setStation(uint8_t stationNumber, char* stationName);
    void setBufferLevel(uint8_t bufferLevel);

    bool resetRequest();

private:
    enum ScreenSelection : uint8_t {
        INIT,
        LOAD,
        PLAY
    };

    constexpr static uint8_t _displayWidth = 132;
    constexpr static uint8_t _displayHeight = 48;
    constexpr static uint8_t _displayMargin = 3;
    constexpr static uint8_t _displayBufferMeterSize = 11;
    constexpr static uint8_t _displayUsableWidthStation = _displayWidth - _displayMargin * 2 - _displayBufferMeterSize;
    constexpr static uint8_t _displayUsableWidthMain = 132 - _displayMargin * 2;
    constexpr static uint8_t _displayFontWidth = 6;
    constexpr static uint8_t _displayTitleTextY = 22;
    constexpr static uint8_t _displayTitleTextLines = 3;
    constexpr static uint8_t _displayLineHeight = 11;
    constexpr static uint8_t _displayStationLineLen = _displayUsableWidthStation / _displayFontWidth;
    constexpr static uint8_t _displayTitleLineLen = _displayUsableWidthMain / _displayFontWidth;

    mutex _sharedStateLock;

    // +++ state in this section must be protected by _sharedStateLock
    CStringBuffer<600, 3> _displayTextBuffer;
    CString _stationText = _displayTextBuffer.allocate(_displayStationLineLen);
    CString _titleText = _displayTextBuffer.allocate();

    ScreenSelection _curScreen = INIT;
    bool _pendingRefresh = true;
    bool _pendingTitleChange = true;

    uint8_t _bufferLevel = 0;
    // --- section end

    // +++ variables in this section may only be accessed by loop core
    ExclusiveMCP23017Gpio _display8080Gpio;
    U8G2_ST7565_KS0713_132x48_8080<1> _u8g2;

    int _titleTopLineOffset = 0;

    bool _displayPagesRemaining = true;
    uint32_t _titlePageChangeIntervalMs = 4000;
    uint32_t _lastTitlePageChangeMillis = 0;
    // --- section end

    int _calcNextTitleLineOffset(int curTopLineOffset);
    static inline int _getUtf8ByteCount(char startByte);
    static inline int _isUtf8NextByte(char b);

    void _renderStation();
    bool _renderTitle();
    void _renderLoadScreen();
    void _renderScrollMarker(bool moreLine);
    void _renderBufferLevel(uint8_t level);
    void _renderFrame();

    bool _selectNextTitlePage();
};
