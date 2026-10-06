# PondPad

Hello Everyone! PondPad is a custom arcade-style macropad built using a Seeed XIAO ESP32-C3. It uses 5 button, a TFT display and Bluetooth for quick shortcut, media, and text actions.

## Project contents

- Firmware: [Firmware/PondPadArcade.ino](Firmware/PondPadArcade.ino)
- Schematic and PCB: [PCB & Schematic/PondPad.kicad_sch](PCB%20%26%20Schematic/PondPad.kicad_sch) and [PCB & Schematic/PondPad.kicad_pcb](PCB%20%26%20Schematic/PondPad.kicad_pcb)
- Gerber output: [Production/Gerber](Production/Gerber)
- CAD files: [CAD](CAD)
- BOM: [BOM.csv](BOM.csv)

## Hardware

The current PCB is designed around:

- 1x Seeed XIAO ESP32-C3-SMD
- 5x Cherry MX-compatible 1.00u PCB switches
- 1x 240x320 ST7789-compatible TFT display
- 1x JST-PH 8-pin display interconnect
- 1x JST-PH 2-pin battery connector
- 4x M2 mounting holes

## BOM

| Name | Qty | Price | Buy Link |
| --- | ---: | --- | --- |
| Cherry MX switch | 5 | $8.75 | [https://www.aliexpress.com/item/1005002162885278.html](Aliexpress) |
| XIAO ESP32-C3 | 1 | $12.00 | [https://www.aliexpress.com/item/1005007469761633.html](Aliexpress) |
| JST PH 1x8 PCB mount | 1 | ~$0.20 | (Most only sells in pack more than 1) |
| JST PH 1x2 PCB mount | 1 | ~$0.20 | (Most only sells in pack more than 1) |
| M2 mounting screw | 4 | ~$0.50 | (Most only sells in pack more than 4) |
| 240x320 TFT display | 1 | $13.00 | [https://www.aliexpress.com/item/1005008772378337.html](Aliexpress) |
| PCB (With Stencil) | 1 | $7.80 | [https://www.aliexpress.com/item/1005012672070003.html](Aliexpress) |
| 3D Printing | 1 | ~$10.00 | - |

This list matches the current BOM in [BOM.csv](BOM.csv).

## Firmware

The firmware is written in Arduino C++.

### Required libraries

Install the following in the Arduino IDE or PlatformIO:

- Adafruit_GFX
- Adafruit_ST7789
- BleKeyboard
- Preferences support is built into the ESP32 core

### Pin map

The firmware expects the following GPIO assignments:

- Display backlight: GPIO 2
- Display reset: GPIO 3
- Display data/command: GPIO 4
- Display chip select: GPIO 5
- Key switches: GPIO 6, 7, 21, 9, 20
- SPI clock: GPIO 8
- SPI MOSI: GPIO 10

## Operation

The device starts in a menu screen and offers:

1. Macro pad mode
2. Fly Catch game
3. Whack-a-Frog game
4. Lily Dodge game
5. High score screen

The macro pad mode sends keyboard shortcuts and media commands over BLE to a paired host.

## License

This project is distributed under the repository license. See the [LICENSE](LICENSE) for details.
