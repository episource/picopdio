#pragma once

#include <string>
#include <charconv>

#include <Client.h>

#include <CString.h>
#include <llhttp.h>

#if !defined(DEBUG_RP2040_PORT) || !defined(DEBUG_ICY_ENABLED)
#define DEBUG_ICY(...) do { } while(0)
#else
#define DEBUG_ICY(...) DEBUGV(__VA_ARGS__)
#endif

#if !defined(DEBUG_RP2040_PORT) || !defined(DEBUG_ICY_DETAILS_ENABLED)
#define DEBUG_ICY_DETAILS(...) do { } while(0)
#else
#define DEBUG_ICY_DETAILS(...) DEBUGV(__VA_ARGS__)
#endif

#if !defined(DEBUG_RP2040_PORT) || !defined(INFO_ICY_ENABLED)
#define INFO_ICY(...) do { } while(0)
#else
#define INFO_ICY(...) DEBUGV(__VA_ARGS__)
#endif

#if !defined(DEBUG_RP2040_PORT) || !defined(INFO_ICY_HEADER_ENABLED)
#define INFO_ICY_HEADER(...) do { } while(0)
#else
#define INFO_ICY_HEADER(...) DEBUGV(__VA_ARGS__)
#endif

template<int _bufferSize, int _metaSize = 4096, int _feedLimit = INT_MAX, bool _autoFeed = true>
class IcyStreamClient final : Client {
public:
    explicit IcyStreamClient(Client &transportClient) : _transportClient(transportClient) {
        _resetAll();
    }

    int bufferFill() {
        return _bufferBodyBytes;
    }

    int bufferFillPercent() {
        return _bufferBodyBytes * 100 / _bufferSize;
    }

    bool bufferFull() const noexcept {
        return _bufferBodyBytes == _bufferSize;
    }

    bool connect(const char *url) noexcept {
        return connect(std::string_view(url));
    }

    bool connect(std::string_view url) noexcept {
        DEBUG_ICY("Connecting to url: %.*s\n", url.length(), url.data());

        auto npos = std::string_view::npos;
        auto endOfSchema = url.find("://");
        auto startOfHost = endOfSchema == npos ? 0 : endOfSchema + 3;
        auto maybeStartOfPath = url.find('/', startOfHost);
        auto maybeStartOfPort = url.find(':', startOfHost);
        auto credentialsSeparator = url.find('@', startOfHost);

        if (maybeStartOfPath == npos) {
            maybeStartOfPath = url.length();
        }

        if (credentialsSeparator != npos && credentialsSeparator < maybeStartOfPath) {
            INFO_ICY("URL contains credentials - not supported.\n");
            return false;
        }

        uint16_t port = 80;
        if (maybeStartOfPort != npos && maybeStartOfPort < maybeStartOfPath) {
            std::string_view portString = url.substr(maybeStartOfPort, maybeStartOfPath - maybeStartOfPort);
            std::errc portResult = std::from_chars(
                    portString.data(), portString.data() + portString.length(), port).ec;
            if (portResult == std::errc::invalid_argument) {
                INFO_ICY("Failed to parse url - invalid port.\n");
                return false;
            }
        }

        return connect(
                url.substr(startOfHost, maybeStartOfPath - startOfHost),
                port, url.substr(maybeStartOfPath));
    }

    bool connect(const char *host, uint16_t port, const char *path) noexcept {
        return connect(std::string_view(host), port, std::string_view(path));
    }

    bool connect(std::string_view host, uint16_t port, std::string_view path) noexcept {
        INFO_ICY("Connecting to http://%.*s:%d%.*s\n", host.length(), host.data(), port, path.length(), path.data());

        // preserve meta data until headers have been sent: host and path might be allocated within metadata buffer
        _resetConnection();

        if (_bufferSize < host.length() + 1) {
            INFO_ICY("buffer size to low: not able to store null terminated host name.");
            return false;
        }

        memcpy(_buffer, host.data(), host.length());
        _buffer[host.length()] = '\0';

        if (!_transportClient.connect((const char*)_buffer, port)) {
            return false;
        }

        using namespace std::literals;
        constexpr std::string_view eol = "\r\n"sv;

        constexpr int portStringSize = 6;
        char portAsString[portStringSize];
        snprintf(portAsString, portStringSize, "%d", port);

        bool result = _writeToTransport({
               "GET "sv, path, " HTTP/1.1"sv, eol,
               "Host: "sv, host, ":"sv, portAsString, eol,
               "User-Agent: IcyStreamClient/1.0"sv, eol,
               "Icy-Metadata: 1"sv, eol,
               "Connection: close"sv, eol,
               eol
       });

        _resetMeta();
        return result;
    }

    bool connect(const IPAddress &ip, uint16_t port, const char *path) noexcept {
        constexpr int ipStringSize = 17;
        char ipAsString[ipStringSize];
        snprintf(ipAsString, ipStringSize, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);

        return connect(ipAsString, port, path);
    }

    /// @brief Read from transport to fill buffer. Same as reading into a zero-sized buffer.
    /// @returns The number of payload bytes available to read; -1 if end of stream.
    int feedBuffer() noexcept {
        if (_bufferBodyBytes == _bufferSize) {
            return _bufferBodyBytes;
        }

        bool maybeError = false;

        // note: _readTransport and _parseBuffer operate on continuous buffer
        // but: unallocated buffer area might be wrapped around the end of the circular buffer
        // => second _readTransport() is needed if unallocated buffer area is wrapped around
        for (IOResult ioresult = IOResult::SUCCESS_MAYBE_MORE_DATA;
             ioresult == IOResult::SUCCESS_MAYBE_MORE_DATA && _bufferBodyBytes < _bufferSize;) {

            uint8_t *dataToParse = nullptr;
            int lengthOfDataToParse = 0;
            ioresult = _readTransport(dataToParse, lengthOfDataToParse);

            bool isParseError = false;
            if (lengthOfDataToParse > 0) {
                int parseResult = llhttp_execute(&_llhttp, (const char *) (dataToParse), lengthOfDataToParse);
                isParseError = parseResult != llhttp_errno::HPE_OK;

                if (isParseError) {
#ifdef ICY_STREAM_CLIENT_ASSERTIONS
                    DEBUGV("llhttp_execute failed or paused: %d (%s)\n", parseResult, llhttp_get_error_reason(&_llhttp));
                    exit(-1);
#endif
                }
            }

            bool isHttpError = (httpStatus() != 200 && httpStatus() != 0);
            maybeError = ioresult == IOResult::END_OF_STREAM || isHttpError || isParseError;

            if (maybeError) {
                llhttp_finish(&_llhttp);
                break;
            }
        }

        if (httpStatus() == 301 || httpStatus() == 302 || httpStatus() == 307 || httpStatus() == 308) {
            if (!_httpLocation.isAllocated()) {
                INFO_ICY("Got http/%d without location header!\n", httpStatus());
                _transportClient.stop();
                return -1;
            }

            INFO_ICY("Redirect (%d) to: %s\n", httpStatus(), _httpLocation.raw());
            if (!connect(_httpLocation.asStringView())) {
                _transportClient.stop();
                return -1;
            }
        } else if (maybeError) {
            _transportClient.stop();
            return -1;
        }

        return _bufferBodyBytes;
    }

    int httpStatus() const noexcept {
        return _llhttp.status_code;
    }

    char* icyGenre() {
        return _icyGenre.raw();
    }

    char* icyName() {
        return _icyName.raw();
    }

    char* icyTitle() {
        return _icyTitle.raw();
    }

    void setOnIcyMetadataChanged(std::function<void()> onIcyMetadataChanged) {
        _onIcyMetadataChanged = onIcyMetadataChanged;
    }

    int readTo(std::function<int(const uint8_t*, int)> consumer) {
        if (_autoFeed) {
            feedBuffer();
        }

        if (_bufferBodyBytes == 0) {
            return 0;
        }

        int bodyEndIndex = (_bufferHeadIndex + _bufferBodyBytes) % _bufferSize;
        int avail = _bufferHeadIndex >= bodyEndIndex ? _bufferSize - _bufferHeadIndex : bodyEndIndex - _bufferHeadIndex;

        int iocount = consumer(_buffer + _bufferHeadIndex, avail);
        iocount = std::max(0, std::min(avail, iocount));

        _bufferBodyBytes -= iocount;
        _bufferHeadIndex = _bufferBodyBytes == 0 ? 0 : (_bufferHeadIndex + iocount) % _bufferSize;

        return iocount;
    }

    /** Client implementation */
    virtual int connect(IPAddress ip, uint16_t port) noexcept override {
        return connect(ip, port, "/");
    }

    virtual int connect(const char *host, uint16_t port) noexcept override {
        return connect(host, port, "/");
    }

    virtual size_t write(uint8_t) noexcept override { return 0; };

    virtual size_t write(const uint8_t *buf, size_t size) noexcept override { return 0; };
    virtual int available() noexcept override {
        if (_autoFeed) {
            feedBuffer();
        }

        return _bufferBodyBytes;
    }

    virtual int read() noexcept override {
        if (_autoFeed) {
            feedBuffer();
        }

        if (_bufferBodyBytes == 0) {
            return -1;
        }

        int result = _buffer[_bufferHeadIndex];

        _bufferBodyBytes--;
        _bufferHeadIndex = (_bufferHeadIndex + 1) % _bufferSize;

        return result;
    }

    /// @brief Client#read implementation. Returns -1 on end of stream.
    virtual int read(uint8_t *buf, size_t size) noexcept override {
        if (!connected()) {
            return -1;
        }

        int iocount = 0;

        while (iocount < size) {
            int count = readTo([buf, size](const uint8_t *data, int dataSize)->int {
                int iocount = std::min((int)size, dataSize);
                memcpy(buf, data, iocount);
                return iocount;
            });

            buf += count;
            size -= count;

            iocount += count;
            if (count == 0) {
                break;
            }
        }

        return iocount;
    }
    virtual int peek() noexcept override {
        if (_bufferBodyBytes == 0) {
            return -1;
        }

        return _buffer[_bufferHeadIndex];
    }

    virtual void flush() noexcept override { /* can't write - nothing to do */};

    virtual void stop() noexcept override {
        _resetAll();
    }

    virtual uint8_t connected() noexcept override {
        return _transportClient.connected();
    }

    virtual operator bool() noexcept override {
        return connected();
    };

private:
    using OwnType = IcyStreamClient<_bufferSize, _metaSize, _feedLimit, _autoFeed>;
    constexpr static int CURRENT_HEADER_IS_METAINT = INT_MIN;

    Client &_transportClient;

    uint8_t _buffer[_bufferSize] = {};
    int _bufferHeadIndex = 0;
    int _bufferBodyBytes = 0;

    CStringBuffer<_metaSize, 5> _metaBuffer;
    CString _icyName = CString::INVALID;
    CString _icyGenre = CString::INVALID;
    CString _icyTitle = CString::INVALID;
    CString _httpLocation = CString::INVALID;
    CString _httpTmp = CString::INVALID;

    std::function<void()> _onIcyMetadataChanged = []() -> void {};

    int _icyMetaInt = 0;

    // ignored if _icyMetaInt == 0, otherwise:
    //  > 0: payload bytes remaining until next icy block
    // == 0: next payload byte is start of icy block
    //  < 0: bytes remaining until end of icy block
    int _icyCounter = 0;

    llhttp_t _llhttp = {};
    llhttp_settings_t _llhttp_settings{
            .on_header_field = _on_header_field_c,
            .on_header_value = _on_header_value_c,
            .on_body = _on_body_c,
            .on_message_complete = _on_message_complete_c,
            .on_header_field_complete = _on_header_field_complete_c,
            .on_header_value_complete = _on_header_value_complete_c,
    };

    enum IOResult {
        SUCCESS_MAYBE_MORE_DATA,
        SUCCESS_NO_MORE_DATA,
        END_OF_STREAM
    };

    int _maxContinuousWrite(int &bufferEndIndex) {
        bufferEndIndex = (_bufferHeadIndex + _bufferBodyBytes) % _bufferSize;
        return _bufferHeadIndex > bufferEndIndex ? _bufferHeadIndex - bufferEndIndex : _bufferSize - bufferEndIndex;
    }

    void _resetAll() noexcept {
        _resetConnection();
        _resetMeta();
    }

    void _resetConnection() noexcept {
        _transportClient.stop();

        _bufferHeadIndex = 0;
        _bufferBodyBytes = 0;
        _icyMetaInt = 0;
        _icyCounter = 0;

        // reset parser
        llhttp_init(&_llhttp, llhttp_type::HTTP_RESPONSE, &_llhttp_settings);
        _llhttp.data = this;
    }

    void _resetMeta() noexcept {
        _metaBuffer.removeAll();

        _httpLocation = CString::INVALID;
        _httpTmp = CString::INVALID;

        _icyName = CString::INVALID;
        _icyGenre = CString::INVALID;
        _icyTitle = CString::INVALID;
    }

    IOResult _readTransport(uint8_t* &dataToParse, int &length) noexcept {
        if (_bufferBodyBytes == _bufferSize) {
            return IOResult::SUCCESS_MAYBE_MORE_DATA;
        }

        int bufferEndIndex;
        int availableForContinuousWrite = _maxContinuousWrite(bufferEndIndex);

        dataToParse = _buffer + bufferEndIndex;
        length = std::min(_feedLimit, availableForContinuousWrite);
        length = _transportClient.read(dataToParse, length);

        // there is no spec for what a Client#read implementation should return if
        //      a) there is no data, but connection is OK
        //      b) connection is closed / end of stream
        // The naive assumption would be: a=0; b<0
        // BUT: EthernetClient returns a=-1; b=0...
        // In order not to depend on a specific Client implementation check connected status on result <=0
        if (length <= 0) {
            if (!_transportClient.connected()) {
                return IOResult::END_OF_STREAM;
            }
            return IOResult::SUCCESS_NO_MORE_DATA;
        }

        return length == availableForContinuousWrite ? SUCCESS_MAYBE_MORE_DATA : SUCCESS_NO_MORE_DATA;
    }

    bool _writeToTransport(std::initializer_list<std::string_view> list) noexcept {
        DEBUG_ICY("Writing to transport...\n");
        for (std::string_view str: list) {
            DEBUG_ICY("%.*s", str.length(), str.data());
            if (str.length() != _transportClient.write((const uint8_t *) str.data(), str.length())) {
                DEBUG_ICY("... failed!\n");
                return false;
            }
        }

        DEBUG_ICY("... done!\n");
        return true;
    }

    void _on_body(uint8_t *at, int length) {
        DEBUG_ICY_DETAILS("_on_body: %d\n", length);
        while (length > 0) {
            if (_icyCounter <= 0) {
                int consumed = _on_body_icy(at, length);
                length -= consumed;
                at += consumed;
            } else if (length > _icyCounter) {
                _on_body_payload(at, _icyCounter);

                length -= _icyCounter;
                at += _icyCounter;
                _icyCounter = 0;
            } else {
                _icyCounter -= length;
                _on_body_payload(at, length);
                break;
            }
        }
    }
    int _on_body_icy(uint8_t* at, int length) {
        DEBUG_ICY_DETAILS("_on_body_icy (length/_icyCounter): %d/%d\n", length, _icyCounter);

        if (length <= 0) {
            return 0;
        }

#ifdef ICY_STREAM_CLIENT_ASSERTIONS
        if (_icyCounter > 0) {
            DEBUGV("ASSERTION FAILED: _icyCounter not <= 0!\n");
            exit(-1);
        }
#endif

        int iocount = 0;

        // this is the start of an icy block
        // first byte is number of bytes that follow
        if (_icyCounter == 0) {
            constexpr int icyBlockLengthScale = 16;
            _icyCounter = -1 * at[0] * icyBlockLengthScale;
            at++;
            length--;
            iocount++;

            DEBUG_ICY_DETAILS("_on_body_icy: new icy block - %d bytes\n", -1 * _icyCounter);

#ifdef ICY_STREAM_CLIENT_ASSERTIONS
            if (_httpTmp.isAllocated()) {
                DEBUGV("ASSERTION FAILED: _httpTmp already in use!\n");
                exit(-1);
            }
#endif

            if (_icyCounter == 0) {
                DEBUG_ICY_DETAILS("_on_body_icy: skipping empty icy block\n");

                _icyCounter = _icyMetaInt;
                return iocount;
            }

            _httpTmp = _metaBuffer.allocate();
        }

        int icyBytes = std::min(length, -1 * _icyCounter);
        _httpTmp.appendMost((const char*)at, icyBytes);

        iocount += icyBytes;
        _icyCounter += icyBytes;

        // done reading the current icy block
        // reset _icyCounter to number of bytes until next block and evaluate title
        if (_icyCounter == 0) {
            DEBUG_ICY_DETAILS("_on_body_icy: end of icy block; raw value = %s\n", _httpTmp.raw());

            _icyCounter = _icyMetaInt;

            // extract StreamTitle
            const char* const titleField = "StreamTitle=";
            int streamTitleIndex = _httpTmp.indexOf(titleField);

            if (streamTitleIndex >= 0) {
                _httpTmp.substring(streamTitleIndex + (int) strlen(titleField));
                int endOfTitleIndex = _httpTmp.indexOf(';');
                if (endOfTitleIndex != 0) {
                    _httpTmp.substring(0, endOfTitleIndex);
                }
                _httpTmp.trim("\";'");
            }

            if (_httpTmp.isEmpty() || streamTitleIndex < 0 || _httpTmp == _icyTitle) {
                // preserve current title if no new stream title was received
                _httpTmp.deallocate();
            } else {
                // only update title if a new stream title was received
                _icyTitle.deallocate();
                _icyTitle = _httpTmp.shrinkToFit();

                DEBUG_ICY_DETAILS("_on_body_icy: new title = %s\n", _icyTitle.raw());
                _onIcyMetadataChanged();
            }

            _httpTmp = CString::INVALID;
        }
        return iocount;
    }
    void _on_body_payload(uint8_t *at, int length) noexcept {
        DEBUG_ICY_DETAILS("_on_body_payload: %d\n", length);

        if (length <= 0) {
            return;
        }

        int atRelativeIndex = at - _buffer - _bufferHeadIndex;
        bool isWrapOver = atRelativeIndex < 0;
        if (isWrapOver) {
            atRelativeIndex += _bufferSize;
        }

        if (_bufferBodyBytes == 0) {
            DEBUG_ICY_DETAILS("_on_body: no body data yet => dropping preceding protocol bytes\n");

            // no body data in buffer: just drop preceding protocol bytes
            _bufferHeadIndex = (_bufferHeadIndex + atRelativeIndex) % _bufferSize;
            _bufferBodyBytes = length;
            return;
        }

        int numProtocolBytesToSkip = atRelativeIndex - _bufferBodyBytes;
        if (numProtocolBytesToSkip == 0) {
            DEBUG_ICY_DETAILS("_on_body: new body data directly follows already buffered body data => nothing to shift\n");

            // new body data directly follows already buffered body data: just increase body byte counter
            _bufferBodyBytes += length;
            return;
        }

        DEBUG_ICY_DETAILS("_on_body: gap found => shift needed\n");

        int remain = length;
        do {
            int curBufferEndIndex;
            int availableForContinuousWrite = _maxContinuousWrite(curBufferEndIndex);

            uint8_t* src = at;
            uint8_t* dst = _buffer + curBufferEndIndex;

            int count = std::min(availableForContinuousWrite, remain);
            memmove(dst, src, count);

            remain -= count;
            _bufferBodyBytes += count;
        } while(remain > 0);
    }
    static int _on_body_c(llhttp_t *llhttp, const char *at, size_t length) noexcept {
        ((OwnType *) llhttp->data)->_on_body((uint8_t*)at, (int)length);
        return HPE_OK;
    }


    void _on_header_field(const char* at, size_t length) noexcept {
        INFO_ICY_HEADER("_on_header_field: %.*s\n", length, at);

        if (!_httpTmp.isAllocated()) {
            _httpTmp = _metaBuffer.allocate();
        }
        _httpTmp.appendMost(at, length);
    }
    static int _on_header_field_c(llhttp_t *llhttp, const char *at, size_t length) noexcept {
        ((OwnType *) llhttp->data)->_on_header_field(at, length);
        return HPE_OK;
    }

    void _on_header_field_complete() noexcept {
        if (!_httpTmp.isAllocated()) {
            DEBUG_ICY_DETAILS("_on_header_field_complete: _httpTmp not allocated!\n");
            return;
        }

        DEBUG_ICY_DETAILS("_on_header_field_complete: %s\n", _httpTmp.raw());

        _httpTmp.toLower();
        if (_httpTmp == "icy-genre") {
            _icyGenre = _httpTmp;
        } else if (_httpTmp == "icy-name") {
            _icyName = _httpTmp;
        } else if (_httpTmp.toLower() == "location") {
            _httpLocation = _httpTmp;
        } else if (_httpTmp.toLower() == "icy-metaint") {
            _icyMetaInt = CURRENT_HEADER_IS_METAINT;
            _httpTmp.clear().shrinkToFit();
        } else {
            _httpTmp.deallocate();
            return;
        }

        _httpTmp.clear().shrinkToFit();
    }
    static int _on_header_field_complete_c(llhttp_t *llhttp) noexcept {
        ((OwnType *) llhttp->data)->_on_header_field_complete();
        return HPE_OK;
    }

    void _on_header_value(const char *at, size_t length) noexcept {
        INFO_ICY_HEADER("_on_header_value: %.*s\n", length, at);

        if (!_httpTmp.isAllocated()) {
            INFO_ICY_HEADER("_on_header_value: skipping\n");
            return;
        }

        _httpTmp.appendMost(at, (int)length);
    }
    static int _on_header_value_c(llhttp_t *llhttp, const char *at, size_t length) {
        ((OwnType *) llhttp->data)->_on_header_value(at, length);
        return HPE_OK;
    }

    void _on_header_value_complete() noexcept {
        INFO_ICY_HEADER("_on_header_value_complete: %s\n", _httpTmp.isAllocated() ? _httpTmp.raw() : "discarded");

        if (_icyMetaInt == CURRENT_HEADER_IS_METAINT) {
            std::errc metaintResult = std::from_chars(
                    _httpTmp.raw(), _httpTmp.raw() + _httpTmp.length(), _icyMetaInt).ec;
            if (metaintResult == std::errc::invalid_argument) {
                DEBUG_ICY("Failed to parse header icy-metaint - invalid number.\n");
                _icyMetaInt = 0;
            } else {
                _icyCounter = _icyMetaInt;
            }

            _httpTmp.deallocate();
        } else if (_httpTmp.isAllocated() && _httpTmp.bufferIndex() != _httpLocation.bufferIndex()){
            _onIcyMetadataChanged();
        }

        _httpTmp = CString::INVALID;
    }
    static int _on_header_value_complete_c(llhttp_t *llhttp) noexcept {
        ((OwnType *) llhttp->data)->_on_header_value_complete();
        return HPE_OK;
    }

    void _on_message_complete() noexcept {
        this->_transportClient.stop();
    }
    static int _on_message_complete_c(llhttp_t *llhttp) noexcept {
        ((OwnType *) llhttp->data)->_on_message_complete();
        return HPE_OK;
    }
};
