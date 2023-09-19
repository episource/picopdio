#include "UserInterface.h"

void UserInterface::begin(bool showInitMessage) {
    _display8080Gpio.begin();

    _u8g2.begin();
    _u8g2.setFlipMode(false);
    _u8g2.setFont(USER_INTERFACE_FONT);
    _u8g2.clearDisplay();

    if (showInitMessage) {
        while (loop() && !resetRequest());
    }
}

bool UserInterface::loop() {
    uint32_t mutexOwner = 0;
    if (!mutex_try_enter(&_sharedStateLock, &mutexOwner)) {
        return _displayPagesRemaining;
    }

    uint32_t nowMs = millis();
    bool isTitleScreen = _curScreen == PLAY;
    if (isTitleScreen && _pendingTitleChange) {
        _pendingRefresh = true;
        _pendingTitleChange = false;
        _displayPagesRemaining = false;

        _lastTitlePageChangeMillis = nowMs;
        _titleTopLineOffset = 0;
    }

    if (isTitleScreen && (_pendingRefresh || !_displayPagesRemaining) && nowMs - _lastTitlePageChangeMillis > _titlePageChangeIntervalMs) {
        _lastTitlePageChangeMillis = nowMs;
        _pendingRefresh = _selectNextTitlePage();
    }

    if (_pendingRefresh) {
        _u8g2.firstPage();

        _pendingRefresh = false;
        _displayPagesRemaining = true;
    }

    if (!_displayPagesRemaining) {
        mutex_exit(&_sharedStateLock);
        return false;
    }

    uint8_t bufLevel = _bufferLevel;
    bool moreLinesAvailable = false;

    _renderStation();
    if (isTitleScreen) {
        moreLinesAvailable = _renderTitle();
    }

    mutex_exit(&_sharedStateLock);

    if (isTitleScreen) {
        _renderScrollMarker(moreLinesAvailable);
    } else if (_curScreen != INIT){
        _renderLoadScreen();
    }

    _renderBufferLevel(bufLevel);
    _renderFrame();

    if (!_u8g2.nextPage()) {
        _u8g2.firstPage();
        _displayPagesRemaining = false;
    }

    return _displayPagesRemaining;
}

void UserInterface::showLoadScreen() {
    mutex_enter_blocking(&_sharedStateLock);

    if (_curScreen != LOAD) {
        _curScreen = LOAD;
        _pendingRefresh = true;
    }

    mutex_exit(&_sharedStateLock);
}

void UserInterface::showLoadScreen(uint8_t stationNumber, char *stationName) {
    mutex_enter_blocking(&_sharedStateLock);

    _stationText.clear().appendMostFormat("%02d %s", stationNumber, stationName);

    _curScreen = LOAD;
    _pendingRefresh = true;

    mutex_exit(&_sharedStateLock);
}

void UserInterface::showTitleScreen() {
    mutex_enter_blocking(&_sharedStateLock);

    _curScreen = PLAY;
    _pendingTitleChange = true;

    mutex_exit(&_sharedStateLock);
}

void UserInterface::showTitleScreen(char *title, uint8_t bufferLevel) {
    mutex_enter_blocking(&_sharedStateLock);

    _bufferLevel = bufferLevel;
    _titleText.resize(0).append(title);

    _curScreen = PLAY;
    _pendingTitleChange = true;

    mutex_exit(&_sharedStateLock);
}

void UserInterface::setTitle(char *title) {
    mutex_enter_blocking(&_sharedStateLock);

    if (_titleText != title) {
        _titleText.resize(0).append(title);
        _pendingTitleChange = true;
    }

    mutex_exit(&_sharedStateLock);
}

void UserInterface::setStation(uint8_t stationNumber, char* stationName) {
    mutex_enter_blocking(&_sharedStateLock);

    _stationText.clear().appendMostFormat("%02d %s", stationNumber, stationName);
    _pendingRefresh = true;

    mutex_exit(&_sharedStateLock);
}

void UserInterface::setBufferLevel(uint8_t bufferLevel) {
    mutex_enter_blocking(&_sharedStateLock);

    if (_bufferLevel != bufferLevel) {
        _bufferLevel = bufferLevel;
        _pendingRefresh = true;
    }

    mutex_exit(&_sharedStateLock);
}

bool UserInterface::resetRequest() {
    return _display8080Gpio.resetRequest();
}

int UserInterface::_calcNextTitleLineOffset(int curTopLineOffset) {
    int mainTextLen = _titleText.length();

    if (curTopLineOffset + _displayTitleLineLen >= mainTextLen) {
        return mainTextLen;
    }

    int bytesInLine = _displayTitleLineLen;
    for (int offset = curTopLineOffset; offset - curTopLineOffset < bytesInLine && offset < mainTextLen;) {
        int utf8Bytes = _getUtf8ByteCount(_titleText[offset]);
        bytesInLine += utf8Bytes - 1;
        offset += utf8Bytes;
    }

    int nextBreakIndex = _titleText.indexOfAny("\n\r", curTopLineOffset);
    if (nextBreakIndex < curTopLineOffset || nextBreakIndex >= curTopLineOffset + bytesInLine) {
        nextBreakIndex = _titleText.lastIndexOfAny(
                " \t\v\f.,-/\\.?!\"'", std::min(mainTextLen - 1, curTopLineOffset + bytesInLine));
        if (nextBreakIndex < curTopLineOffset) {
            nextBreakIndex = curTopLineOffset + bytesInLine;
        } else if (nextBreakIndex - curTopLineOffset < bytesInLine) {
            nextBreakIndex++;
        }
    }

    while (nextBreakIndex < mainTextLen
           && (isspace(_titleText[nextBreakIndex]) || _isUtf8NextByte(_titleText[nextBreakIndex]))) {
        nextBreakIndex++;
    }
    return nextBreakIndex;
}

int UserInterface::_getUtf8ByteCount(char startByte) {
    if ((startByte & 0x80) != 0x80) {
        return 1;
    }
    if ((startByte & 0xe0) == 0xc0) {
        return 2;
    }
    if ((startByte & 0xf0) == 0xe0) {
        return 3;
    }
    if ((startByte & 0xf8) == 0xf0) {
        return 4;
    }
    return 1;
}

int UserInterface::_isUtf8NextByte(char b) {
    return (b & 0xc0) == 0x80;
}

void UserInterface::_renderStation() {
    if (_curScreen != INIT) {
        _u8g2.drawStr(_displayMargin, 10, _stationText.raw());
    } else {
        _u8g2.drawStr(_displayMargin, 10, "Bitte warten...");
    }
}

void UserInterface::_renderLoadScreen() {
    _u8g2.drawUTF8(_displayMargin, _displayTitleTextY, "Lädt...");
}

bool UserInterface::_renderTitle() {
    int mainTextLen = _titleText.length();
    int curLineOffset = _titleTopLineOffset;

    for (int i = 0; i < _displayTitleTextLines && curLineOffset < mainTextLen; ++i) {
        int nextLineOffset = _calcNextTitleLineOffset(curLineOffset);
        int curLineLen = nextLineOffset - curLineOffset;

        CString line = _titleText.cloneWithLimit(
                curLineOffset, curLineLen);

        curLineOffset = nextLineOffset;

        if (line.isAllocated()) {
            _u8g2.drawUTF8(_displayMargin, _displayTitleTextY + i * _displayLineHeight, line.raw());
            line.deallocate();
        }
    }

    return curLineOffset < mainTextLen;
}

void UserInterface::_renderScrollMarker(bool moreLines) {
    if (_titleTopLineOffset != 0) {
        // show indicator that there's more text above
        _u8g2.drawHLine(_displayWidth - 3, 16, 1);
        _u8g2.drawHLine(_displayWidth - 4, 15, 2);
        _u8g2.drawHLine(_displayWidth - 5, 14, 3);
    }

    if (moreLines) {
        // show indicator that there's more text below
        _u8g2.drawHLine(_displayWidth - 3, _displayHeight - 5, 1);
        _u8g2.drawHLine(_displayWidth - 4, _displayHeight - 4, 2);
        _u8g2.drawHLine(_displayWidth - 5, _displayHeight - 3, 3);
    }
}

void UserInterface::_renderBufferLevel(uint8_t level) {
    _u8g2.drawHLine(119, 10, 2);
    _u8g2.drawHLine(122, 10, 2);
    _u8g2.drawHLine(125, 10, 2);
    _u8g2.drawHLine(128, 10, 2);

    if (_curScreen == LOAD || _curScreen != PLAY) {
        return;
    }

    if (level >= 4) {
        _u8g2.drawVLine(128, 2, 8);
        _u8g2.drawVLine(129, 2, 8);
    }
    if (level >= 3) {
        _u8g2.drawVLine(125, 4, 6);
        _u8g2.drawVLine(126, 4, 6);
    }
    if (level >= 2) {
        _u8g2.drawVLine(122, 6, 5);
        _u8g2.drawVLine(123, 6, 5);
    }
    if (level >= 1) {
        _u8g2.drawVLine(119, 8, 2);
        _u8g2.drawVLine(120, 8, 2);
    }
}

void UserInterface::_renderFrame() {
    _u8g2.drawFrame(0, 0, _displayWidth, _displayHeight);
    _u8g2.drawHLine(0, 12, _displayWidth);
}

bool UserInterface::_selectNextTitlePage() {
    int titleLen = _titleText.length();
    int oldOffset = _titleTopLineOffset;

    for (int i = 0; i < _displayTitleTextLines; ++i) { // skip to next page
        _titleTopLineOffset = _calcNextTitleLineOffset(_titleTopLineOffset);

        if (_titleTopLineOffset >= titleLen) {
            _titleTopLineOffset = 0;
            break;
        }
    }

    return _titleTopLineOffset != oldOffset;
}
