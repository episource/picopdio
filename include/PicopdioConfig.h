#pragma once

#include <IniFile.h>
#include <IPAddress.h>

#include <CString.h>

using namespace arduino;

extern void printIniError(IniFile &ini);

template<int _maxStations, int _bufferSize> class PicopdioConfig final {
public:
    bool init(const char* filename) {
        _clear();

        IniFile ini("picopdio.ini");
        bool result = _initInternal(ini);
        if (!result) {
            printIniError(ini);
            _clear();
        }

        ini.close();
        return result;
    }

    int numStations() {
        return _buffer.numstrings() / 2;
    }

    const CString getStationName(int stationIndex) {
        return _buffer.getCString(stationIndex * 2 + 1);
    }

    const CString getStationUrl(int stationIndex) {
        return _buffer.getCString(stationIndex * 2);
    }

    const IPAddress& getIp() {
        return _ip;
    }

    const IPAddress& getGateway() {
        return _gateway;
    }

    const IPAddress& getDns() {
        return _dns;
    }

    const uint8_t* getMac() {
        return _mac;
    }

private:
    IPAddress _ip;
    IPAddress _gateway;
    IPAddress _dns;
    uint8_t _mac[6];

    CStringBuffer<_bufferSize, _maxStations * 2> _buffer;

    void _clear() {
        _ip = (uint32_t)0;
        _gateway = (uint32_t)0;
        _dns = (uint32_t)0;
        std::fill_n(_mac, std::size(_mac), 0);

        _buffer.removeAll();
    }

    boolean _initInternal(IniFile &ini) {
        if (!ini.open()) {
            return false;
        }

        CString tmp = _buffer.allocateRemaining();
        if (!_readNetworkConfig(ini, tmp)) {
            tmp.deallocate();
            return false;
        }
        tmp.deallocate();

        constexpr int maxSectionnameLength = /* station */ 7 + /* index as decimal */ (_maxStations / 10 + 1) + /* \0 */ 1;
        char stationSectionName[maxSectionnameLength];
        for (int i = 0; i < _maxStations; ++i) {
            snprintf(stationSectionName, maxSectionnameLength, "station%02d", i);
            if (!_readValueAsCstring(ini,stationSectionName, "url")) {
                if (ini.getError() == IniFile::error_t::errorSectionNotFound) {
                    DEBUGV("%s not configured - skipping\n", stationSectionName);
                    continue;
                } else {
                    printIniError(ini);
                    return false;
                }
            }

            if (!_readValueAsCstring(ini,stationSectionName, "name")) {
                if (ini.getError() == IniFile::error_t::errorKeyNotFound) {
                    DEBUGV("%s:name unspecified - using default\n", stationSectionName);
                    _buffer.push("(unnamed)");
                } else {
                    printIniError(ini);
                    return false;
                }
            }
        }

        return true;
    }

    boolean _readNetworkConfig(IniFile &ini, CString &buffer) {
        if (!ini.getMACAddress("network", "mac", buffer.raw(), buffer.rawCapacity(), _mac)) {
            return false;
        }
        if (!ini.getIPAddress("network", "ip", buffer.raw(), buffer.rawCapacity(), _ip)) {
            return false;
        }
        if (!ini.getIPAddress("network", "gateway", buffer.raw(), buffer.rawCapacity(), _gateway)) {
            return false;
        }
        if (!ini.getIPAddress("network", "gateway", buffer.raw(), buffer.rawCapacity(), _dns)) {
            return false;
        }

        DEBUGV("# Network config\n    MAC: %02x:%02x:%02x:%02x:%02x:%02x\n    IP: %d.%d.%d.%d    \n    Gateway: %d.%d.%d.%d\n    DNS: %d.%d.%d.%d\n",
               _mac[0], _mac[1], _mac[2], _mac[3], _mac[4], _mac[5],
               _ip[0], _ip[1], _ip[2], _ip[3],
               _gateway[0], _gateway[1], _gateway[2], _gateway[3],
               _dns[0], _dns[1], _dns[2], _dns[3]);

        return true;
    }

    boolean _readValueAsCstring(IniFile &ini, const char* section, const char* key) {
        CString str = _buffer.allocateRemaining();
        boolean result = ini.getValue(section, key, str.raw(), str.rawCapacity());
        if (!result) {
            str.deallocate();
        }

        str.shrinkToFit();
        return result;
    }
};