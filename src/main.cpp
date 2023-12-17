#include <Arduino.h>
#include <Ethernet3.h>
#include <pico.h>
#include <SdFat.h>
#include <SPI.h>
#include <Wire.h>

#include <RPi_Pico_TimerInterrupt.h>
#include <SparkFunSX1509.h>
#include <VS1053.h>

#include <hardware/watchdog.h>

#include "PicopdioDefinitions.h"

#include "PicopdioConfig.h"
#include "UserInterface.h"
#include "RotaryDecoder.h"
#include "IcyStreamClient.h"
#include "WM8711.h"


RPI_PICO_Timer khzHeartbeatTimer(0);
PicopdioConfig<10, 1000> picopdioConfig;
UserInterface ui(0x20, WIRE_HIGH_SPEED, 1, 1, 0, 4, 2, 3, 5);
RotaryDecoder rotdec;
EthernetClient ethClient;
VS1053 player(VS1053_CS, VS1053_DCS, VS1053_DREQ);
WM8711 dac(WIRE_LOW_SPEED, 0x1b);

// global variable required by IniFile library
SdFat SD;

// best buffer size: 8k - 32k
// feedLimit = 32000: EthernetClient shows nondeterministic behavior for larger sizes
IcyStreamClient<16000, 4096, 32000, false> icyStream(ethClient);

volatile uint8_t wantedStationIdx = 0;
critical_section_t wantedStationUpdateSection;

volatile bool core0SetupDone = false;
volatile bool playing = true;


SX1509 sx1509;

#define KEY_ROWS 2
#define KEY_COLS 3
#define KEY_UNUSED 0xff
#define KEY_DIM 0xfe
uint8_t keyMap[KEY_ROWS][KEY_COLS] = {
        {KEY_DIM, KEY_UNUSED, KEY_UNUSED},
        {2, 1, 0}
};

void setupOutLine(pin_size_t pin, PinStatus init);
void setupCsLine(pin_size_t pin);
void reboot();

void storeStation(uint8_t stationIdx) {
    File32 stationFile = SD.open(STATION_FILE, FILE_WRITE);
    stationFile.truncate(0);
    stationFile.write(stationIdx);
    stationFile.close();
}

uint8_t restoreStation() {
    File32 stationFile = SD.open(STATION_FILE, FILE_READ);
    uint8_t result = stationFile.available() ? (uint8_t) stationFile.read() : 0;
    stationFile.close();

    if (result < 0) {
        return 0;
    } else if (result >= picopdioConfig.numStations()) {
        return picopdioConfig.numStations() - 1;
    }

    return result;
}

bool khzHeartbeat(struct repeating_timer *t) {
    RotaryDecoder::Step step = rotdec.readAndDecode(CHANNEL_SELECTOR_ROT_HIGH, CHANNEL_SELECTOR_ROT_LOW);
    if (step == RotaryDecoder::NO_STEP) {
        return true;
    }

    critical_section_enter_blocking(&wantedStationUpdateSection);

    uint8_t maxStationIdx = picopdioConfig.numStations() - 1;
    if (step == RotaryDecoder::BACKWARD && wantedStationIdx == 0) {
        wantedStationIdx = maxStationIdx;
    } else if (step == RotaryDecoder::FORWARD && wantedStationIdx == maxStationIdx) {
        wantedStationIdx = 0;
    } else {
        wantedStationIdx += step;
    }

    critical_section_exit(&wantedStationUpdateSection);

    return true;
}

void setup() {
    critical_section_init(&wantedStationUpdateSection);

    //Serial.begin(115200);
    Serial1.begin(115200);

    SPI_MAIN.begin(false);
    SPI_DEDICATED_SD.begin(false);

    while (true) {
        DEBUGV("Initializing...\n");

        setupCsLine(ETH_CS);
        setupCsLine(SD_CS);
        setupCsLine(VS1053_CS);
        setupCsLine(VS1053_DCS);

        // configure reset line (no reset)
        delay(DEFAULT_INIT_DELAY_MS);
        setupOutLine(SPI_COMPONENTS_RESET_LOW, HIGH);

        if (!SD.begin(SdSpiConfig(SD_CS, DEDICATED_SPI, SPI_DEDICATED_SD_SPEED, &SPI_DEDICATED_SD))) {
            DEBUGV("Failed to init SD\n");
            continue;
        }

        if (!picopdioConfig.init(CONFIG_FILE)) {
            DEBUGV("Failed to read configuration from SD card!\n");
            continue;
        }

        if (picopdioConfig.numStations() == 0) {
            DEBUGV("No stations found!\n");
            continue;
        }

        // no need to enter critical section here:
        // parallelism starts when core0 is ready
        wantedStationIdx = restoreStation();


        // Reset other spi slaves
        digitalWrite(SPI_COMPONENTS_RESET_LOW, LOW);
        delay(DEFAULT_INIT_DELAY_MS);
        digitalWrite(SPI_COMPONENTS_RESET_LOW, HIGH);

        delay(DEFAULT_INIT_DELAY_MS);
        Ethernet.setCsPin(ETH_CS);
        Ethernet.init(2);
        Ethernet.begin(
                const_cast<uint8_t *>(picopdioConfig.getMac()),
                picopdioConfig.getIp(), picopdioConfig.getDns(), picopdioConfig.getGateway());

        switch (picopdioConfig.compatMode()) {
            case ETH_FULL_AUTONEG:
                Ethernet.phyMode(ALL_AUTONEG);
                break;
            case ETH_100_HD:
                Ethernet.phyMode(HALF_DUPLEX_100);
                break;
            case ETH_10_HD:
                Ethernet.phyMode(HALF_DUPLEX_10);
                break;
            case ETH_HW_DEFAULT:
            default:
                // nothing to do
                break;
        }


        player.begin();

        // 4 == VS1053;
        if (player.getChipVersion() != 4) {
            DEBUGV("Unknown VLSI chip %d\n");
            continue;
        }
        player.loadDefaultVs1053Patches();

        //player.switchToMp3Mode(); // optional, some boards require this
        player.setVolume(0);
        player.enableI2sOut();

        break;
    }

    icyStream.setOnIcyMetadataChanged([]() -> void {
        if (playing) {
            ui.setTitle(icyStream.icyTitle());
        }
    });

    core0SetupDone = true;
}

void loop() {
    static uint8_t connectedStationIdx = UINT8_MAX;
    static int minBuffer = 100;
    static uint8_t bufLevel = 4;
    static uint32_t lastBufLevelUpdateMillis = 0;

    uint8_t requestedStationIdx = wantedStationIdx;
    uint32_t nowMs = millis();
    if (!icyStream.connected() || !Ethernet.link() || requestedStationIdx != connectedStationIdx) {
        if (playing) {
            player.setVolume(0);
            player.stopSong();

            playing = false;
        }

        ui.showLoadScreen();

        minBuffer = 0;
        bufLevel = 0;

        if (!Ethernet.link()) {
            icyStream.stop();
            return;
        } else {
            DEBUGV("Link ready - PHY state: 0x%02x\n", Ethernet.phyState());

            storeStation(requestedStationIdx);
            icyStream.connect(picopdioConfig.getStationUrl(requestedStationIdx));
            connectedStationIdx = requestedStationIdx;
        }
    } else if (nowMs - lastBufLevelUpdateMillis > 1000) {
        lastBufLevelUpdateMillis = nowMs;

        if (minBuffer > 80 || icyStream.bufferFull()) {
            bufLevel = 4;
        } else if (minBuffer > 55) {
            bufLevel = 3;
        } else if (minBuffer > 30) {
            bufLevel = 2;
        } else if (minBuffer > 5) {
            bufLevel = 1;
        } else {
            bufLevel = 0;
        }

        minBuffer = 100;
        ui.setBufferLevel(bufLevel);
    }

    const int read = icyStream.feedBuffer();

    static uint32_t lastFeedMs = nowMs;
    if (read > 0) {
        lastFeedMs = nowMs;
    } else if (nowMs - lastFeedMs > 5000) {
        DEBUGV("feedBuffer: no data available for too long! Resetting!\n");
        reboot();
    }

    minBuffer = std::min(minBuffer, icyStream.bufferFillPercent());

    if (!playing) {
        if (!icyStream.bufferFull()) {
            return;
        }

        player.startSong();
        player.setVolume(VOLUME);
        playing = true;

        lastBufLevelUpdateMillis = nowMs;
        minBuffer = 100;
        bufLevel = 4;

        ui.showTitleScreen(icyStream.icyTitle(), 4);
    }

    icyStream.readTo([](const uint8_t *data, int size) -> int {
        return player.playNonBlocking(data, size);
    });
}

void setup1() {
    // activate LCD backlight as early as possible to provide user feedback
    pinMode(LCD_BG, ANALOG_OUTPUT);
    analogWrite(LCD_BG, LCD_DEFAULT_BRIGHTNESS);

    // reset I2C slaves
    setupOutLine(I2C_COMPONENTS_RESET_LOW, LOW);
    delay(DEFAULT_INIT_DELAY_MS);
    digitalWrite(I2C_COMPONENTS_RESET_LOW, HIGH);

    pinMode(CHANNEL_SELECTOR_ROT_HIGH, INPUT_PULLUP);
    pinMode(CHANNEL_SELECTOR_ROT_LOW, INPUT_PULLUP);

    WIRE_LOW_SPEED.setSCL(WIRE_LOW_SPEED_SCL);
    WIRE_LOW_SPEED.setSDA(WIRE_LOW_SPEED_SDA);
    WIRE_LOW_SPEED.setClock(400000);
    WIRE_LOW_SPEED.begin();

    WIRE_HIGH_SPEED.setSCL(WIRE_HIGH_SPEED_SCL);
    WIRE_HIGH_SPEED.setSDA(WIRE_HIGH_SPEED_SDA);
    // rp2040 is specified to support Fast Mode+ (up to 1MHz Baud Rate)
    // it has 80k builtin internal pull-ups
    // MCP23017 is specified to support baud rate up to 1.7MHz
    // - using external pull-ups rp2040 can do more than 1MHz, though
    // - using external 10k pull-ups, 1.35MHz baud rate seem to work most
    //   of the time, up to 1.30MHZ stable,
    // - using lower pull-ups does not help to achieve higher rate
    // - note though, that going >1MHZ makes the bus very sensitive to noise
    //   on breadboard designs
    WIRE_HIGH_SPEED.setClock(999999);
    WIRE_HIGH_SPEED.begin();

    // early display of gui!
    ui.begin(true);
    if (ui.resetRequest()) {
        DEBUGV("Failed to initialize display. Rebooting...\n");
        reboot();
    }

    // start dac + shaft and key decoding only after core 0 is ready
    // => stations not available before SD card has been read
    // => MCLK not ready
    while (!core0SetupDone) {
        yield();
    }

    // wait for core0 for MCLK to be ready
    dac.begin();

    // wait for core0 to finish reading PicopdioConfig
    khzHeartbeatTimer.attachInterrupt(1000, khzHeartbeat);

    sx1509.begin(0x3E, WIRE_LOW_SPEED);
    sx1509.reset(false);

    // normal buttons: debounce time 4ms is good
    // rotary switch button: debounce time must be >= 16ms!
    sx1509.keypad(KEY_ROWS, KEY_COLS, 0, 32, 16);

    DEBUGV("Core1 ready!\n");
}

void loop1() {
    if (ui.resetRequest()) {
        DEBUGV("Resetting display...\n");
        ui.begin(false);
    }

    uint32_t now = millis();

    uint16_t keyData = 0;
    static uint32_t lastSx1509KeydataMs = 0;
    if (now - lastSx1509KeydataMs > 100) {
        lastSx1509KeydataMs = now;
        keyData = sx1509.readKeyData();
    }
    if (keyData != 0) {
        uint8_t row = sx1509.getRow(keyData);
        uint8_t col = sx1509.getCol(keyData);

        if (row < KEY_ROWS && col < KEY_COLS) {
            uint8_t key = keyMap[row][col];
            if (key == KEY_DIM) {
                constexpr uint8_t brightnessIncrement = (LCD_MAX_BRIGHTNESS - LCD_MIN_BRIGHTNESS) / LCD_BRIGHTNESS_STEPS;
                static uint8_t lcdBrightness = LCD_DEFAULT_BRIGHTNESS;

                if (lcdBrightness == LCD_MIN_BRIGHTNESS) {
                    lcdBrightness = LCD_MAX_BRIGHTNESS;
                } else {
                    lcdBrightness -= std::min(brightnessIncrement, lcdBrightness);

                    if (lcdBrightness < LCD_MIN_BRIGHTNESS) {
                        lcdBrightness = LCD_MAX_BRIGHTNESS;
                    }
                }

                analogWrite(LCD_BG, lcdBrightness);
            } else if (key != KEY_UNUSED) {
                critical_section_enter_blocking(&wantedStationUpdateSection);
                wantedStationIdx = key;
                critical_section_exit(&wantedStationUpdateSection);
            }
        }
    }

    static bool muted = false;
    if (muted == playing) {
        muted = !playing;
        if (muted) {
            dac.mute();
            dac.setHeadphoneVolume(0);
        } else {
            dac.setHeadphoneVolume(VOLUME);
            dac.unmute();
        }
    }

    uint8_t requestedStationIdx = wantedStationIdx;
    static uint8_t currentDisplayStationIdx = UINT8_MAX;
    if (requestedStationIdx != currentDisplayStationIdx) {
        ui.showLoadScreen(requestedStationIdx + 1, picopdioConfig.getStationName(requestedStationIdx).raw());
        currentDisplayStationIdx = requestedStationIdx;
    }

    ui.loop();
}

void setupOutLine(pin_size_t pin, PinStatus init) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, init);
}

void setupCsLine(pin_size_t pin) {
    setupOutLine(pin, HIGH);
}

void reboot() {
    watchdog_reboot(0, 0, 10);
    while (true) {
        yield();
    }
}
