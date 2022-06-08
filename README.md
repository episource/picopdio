# About
Another internet radio player project reusing parts and case of an old DNT IPdio Tune internet radio, that stopped working due to the shutdown of Reciva services.

The project is build using PlatformIO and [arduino-pico](https://github.com/earlephilhower/arduino-pico) core around a [Raspberry Pi Pico](https://www.raspberrypi.com/products/raspberry-pi-pico/) microcontroller. It uses nodejs' [llhttp](https://github.com/nodejs/llhttp) parser for reading icecast radio streams.

Configuration is stored on an SD card placed into the radio's front panel SD card slot.

# Parts
This is the list of parts used in the project. See the [schematics](schematics/picopdio.pdf) for details.

 * DNT IPdio Tune
    - Case
    - Mother board including Wolfson WM8711B audio codec/DAC and power supply
    - Front Panel including some unknown Tinsharp 132x48 LCD (ST7565 compatible)
 * [Raspberry Pi Pico](https://www.raspberrypi.com/products/raspberry-pi-pico/)
 * [Adafruit VS1053B Breakout](https://learn.adafruit.com/adafruit-vs1053-mp3-aac-ogg-midi-wav-play-and-record-codec-tutorial/downloads-and-links)
 * [Microchip MCP23017 IO Expander](https://www.microchip.com/en-us/product/mcp23017)
 * [Sparkfun SX1509 IO Expander](https://www.sparkfun.com/products/13601)
 * [WIZnet W850io SPI ethernet shield](https://www.wiznet.io/product-item/wiz850io/) ~~WIZnet W5500 based no-name `W5500 Lite` SPI ethernet adapter~~ 
   - no-name W850io clones had auto negotiation issues with some switches 
   - therefore replaced by WIZnet made W850io shield
   - note: disabling auto negotiation and forcing 100MBit or 10MBit Half Duplex instead solved these issues with the no-name clones as well
   - see also WIZnet forums for a [similar issue](https://forum.wiznet.io/t/topic/5529)
 * [Pololu Mini Pushbutton Power Switch #2808](https://www.pololu.com/product/2808)

# Further Reading
Below some links with background information on reciva based internet radios
 * [Sharpfin Webpage](https://www.sharpfin.org/)
 * [Sharpfin Repo](https://github.com/philsmd/sharpfin)
 * [Reciva Barracuda Radio Module Datasheet](https://elinux.org/images/5/5f/Barracuda_1.6.pdf)