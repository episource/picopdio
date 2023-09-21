#include <stdint.h>
#include <debug_internal.h>

#include "PicopdioConfig.h"

void printIniError(IniFile &ini)
{
    switch (ini.getError()) {
        case IniFile::errorNoError:
            DEBUGV("no error");
            break;
        case IniFile::errorFileNotFound:
            DEBUGV("file not found");
            break;
        case IniFile::errorFileNotOpen:
            DEBUGV("file not open");
            break;
        case IniFile::errorBufferTooSmall:
            DEBUGV("_buffer too small");
            break;
        case IniFile::errorSeekError:
            DEBUGV("seek error");
            break;
        case IniFile::errorSectionNotFound:
            DEBUGV("section not found");
            break;
        case IniFile::errorKeyNotFound:
            DEBUGV("key not found");
            break;
        case IniFile::errorEndOfFile:
            DEBUGV("end of file");
            break;
        case IniFile::errorUnknownError:
            DEBUGV("unknown error");
            break;
        default:
            DEBUGV("unknown error value");
            break;
    }

    DEBUGV("\n");
}