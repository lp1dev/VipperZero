# Evil Crow RF V2 — Serial Firmware

A serial-controlled firmware for the Evil Crow RF V2 (ESP32 + dual CC1101).
It replaces the stock WiFi web panel with a line-based command interface over
USB serial, so the device can be driven from a terminal, a script, or the
provided C / Python clients.

**Version:** 1.0.0

## Features

- Sub-GHz RX / TX / jammer on either CC1101 module
- CC1101 presets (AM270, AM650, FM238, FM4768)
- Frequency (RSSI) scanner
- Flipper `.sub` file save / list / send / delete on the SD card
- Binary (OOK symbol) capture and transmit
- nRF24 receive-only channel scanner (requires a soldered nRF24L01+ module)
- BLE central: scan, GATT enumerate, read / write / subscribe
  (the device never advertises itself — central role only)

## Hardware

- Evil Crow RF V2 (classic ESP32 / WROOM-32 with a CP2102N USB-UART bridge)
- USB-C / micro-USB **data** cable (not a charge-only cable)
- Optional: microSD card (FAT32) for `.sub` storage and logs
- Optional: nRF24L01+ module for `nrfscan` (see Pinout below)

## Requirements

Install these once in the Arduino IDE.

1. **ESP32 board support** — in *File → Preferences → Additional Boards
   Manager URLs* add:

   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```

   Then *Tools → Board → Boards Manager*, search **esp32**, install the
   **esp32 by Espressif Systems** package (3.x).

2. **Libraries** — *Tools → Manage Libraries*:
   - **RF24** by TMRh20 (provides `nRF24L01.h` / `RF24.h`)
   - The BLE and SD libraries ship with the ESP32 core — nothing to install.

   > If you do not have an nRF24 module and would rather not install RF24,
   > the `nrfscan` / `nrflog` commands are the only ones that need it; the
   > rest of the firmware compiles and runs without the radio attached once
   > the library is present.

## Installing the firmware

1. Open `firmware.ino` in the Arduino IDE.
   (Keep it in a folder named `firmware/` — the Arduino IDE requires the
   sketch file and its folder to share a name.)
2. *Tools → Board* → **ESP32 Dev Module**.
3. Recommended *Tools* settings:
   - **Flash Size:** 4MB
   - **Partition Scheme:** *Huge APP (3MB No OTA/1MB SPIFFS)* or
     *Minimal SPIFFS* — the BLE stack is large, so the default scheme may
     overflow.
   - **Upload Speed:** 921600 (drop to 115200 if uploads fail)
4. Plug in the board and select the port under *Tools → Port*
   (it appears as `/dev/ttyUSB0` on Linux, `/dev/tty.SLAB_USBtoUART` on
   macOS, or `COMx` on Windows — **not** `ttyACM`, this board uses a
   USB-UART bridge).
5. Click **Upload**. Wait for `Hash of data verified` / `Hard resetting`.

> If `nRF24L01.h: No such file or directory` appears, the RF24 library is
> not installed — see Requirements step 2.

## Connecting

Serial settings: **115200 baud, 8N1, newline (LF) line ending.**

Any terminal works:

```sh
# picocom
picocom -b 115200 --omap crcrlf /dev/ttyUSB0

# screen
screen /dev/ttyUSB0 115200
```

The board resets when the port is opened (the USB-UART bridge toggles the
reset line), then prints:

```
Evil Crow RF V2 v1.0.0 (serial mode). Type 'help'.
```

Type `help` for the full command list. Every command replies with a line
beginning `OK` or `ERR`, which makes the protocol easy to script.

### Linux permissions

If you get *permission denied* on the port:

```sh
sudo usermod -aG dialout $USER   # then log out and back in
```

## Quick start

```
help
status
scan
rx 1 433.92 2 200 0 5
```

`rx` arguments: `<module> <freq_MHz> <modulation> <rxbw> <deviation> <datarate>`
where modulation `2` = ASK/OOK and `0` = 2-FSK, and module is `1` or `2`.
With echo on (default), received frames stream back as they are decoded.

Save a capture and replay it:

```
subsave mycapture
sublist
subsend 1 mycapture
```

## nRF24 pinout (optional)

Solder an nRF24L01+ module to these pins, then `nrfscan` works:

| nRF24 | ESP32 GPIO |
|-------|------------|
| CE    | 32         |
| CSN   | 33         |
| SCK   | 14 (shared with CC1101) |
| MISO  | 12         |
| MOSI  | 13         |
| VCC   | 3.3V (add a 10µF cap across VCC/GND) |
| GND   | GND        |

`nrfscan` is **receive-only** — it reports which 2.4 GHz channels carry
energy and never transmits.

## Command reference

Run `help` on the device for the authoritative, up-to-date list. Summary:

| Area | Commands |
|------|----------|
| Core | `help`, `status`, `reboot`, `echo on\|off` |
| Sub-GHz RX/TX | `rx`, `rxp`, `stoprx`, `tx`, `txp`, `txbin`, `lastbin` |
| Jammer | `jammer`, `stopjammer` |
| Presets / scan | `presets`, `scan` |
| Flipper `.sub` | `subsave`, `sublist`, `subsend`, `subdel` |
| nRF24 (RX-only) | `nrfscan`, `nrflog` |
| BLE central | `blescan`, `blegatt`, `bleconnect`, `bledisconnect`, `blestatus`, `blemtu`, `bleread`, `blewrite`, `blewriten`, `blesub`, `bleunsub`, `blelog` |

## Clients

- `evilcrow.py` — Python client (pyserial) wrapping every command.
- `serial.c` / `serial.h` — minimal C serial library.

## Legal

Transmitting and jamming on sub-GHz / 2.4 GHz bands is regulated in most
jurisdictions. Only transmit on frequencies and devices you are authorized
to test. You are responsible for complying with local law.