# ESP32 OBD-II adapter

This project is work-in-progress and starts as a tool to access the CAN buses available on my Mazda3, for reverse engineering and for fun.

Development is done on an ESP32 DevKitC V4 board, which includes an ESP32-WROOM-32D module.

Built and tested with [ESP-IDF v5.1](https://github.com/espressif/esp-idf).

## Features roadmap

- ✔ CAN connection with vehicle
    - Tested CAN transceivers: [SN65HVD230](https://www.ti.com/product/SN65HVD230), [MCP2551](https://www.microchip.com/en-us/product/MCP2551) (with 5V -> 3.3V RX voltage translation)
- ✔ Linux SocketCAN compatibility with [`slcan` driver](https://github.com/torvalds/linux/blob/v6.0/drivers/net/can/slcan)
- ✔ Bluetooth
    - `slcan` over Bluetooth Classic Serial Port Profile (SPP)
    - legacy pairing PIN is 0000
- ❔ logging to SD card
    - same file format as `candump`?
- ❔ socket communication over WiFi softAP
    - how to keep compatibility with `slcan`? just make a TCP-serial bridge with `socat`? any other way to create a SocketCAN interface while still using `slcan` over TCP?
    - interesting: `python-can` supports "[remote serial ports](https://python-can.readthedocs.io/en/master/interfaces/slcan.html)"
    - TCP unicast or UDP multicast?
    - payload serialization strategy?
- ❔ OBD diagnostics
- ❔ reverse engineered Mazda-specific messages
- ❔ custom protocol for better efficiency?
- ❔ ...

## ESP32 JTAG Debugging

Use the VSCode ESP-IDF extension.

Check `idf.openOcdConfigs` in your `settings.json` (currently set up for J-Link).

## Useful info

### Linux SocketCAN / can-utils usage example

This adapter implements the LAWICEL SLCAN protocol as expected by the `slcan` SocketCAN driver, so that it can be used with [`can-utils`](https://github.com/linux-can/can-utils)' `slcand` and other utilities like `cansniffer`.

Set up CAN interface:
```sh
sudo slcand -o -c -s6 -S 921600 /dev/ttyUSB0 slcan0 # add -F to run in foreground, use /dev/rfcomm0 for Bluetooth serial port
```

Expose with [`socketcand`](https://github.com/linux-can/socketcand):
```sh
sudo socketcand -v -i slcan0
```

Sniff bus in real time:
```sh
sudo ip link set up slcan0
cansniffer slcan0
```

Dump to file:
```sh
candump -l slcan0
```

Replay from file:
```sh
sudo ip link add dev vcan0 type vcan
sudo ip link set up vcan0
# Replay slcan0 dump on vcan0 in infinite loop
canplayer vcan0=slcan0 -li -I ./candump-*.log
```

### OBD-II pin and cable mapping

Ethernet twisted pair cable should help with signal integrity (it's designed to carry differential signals).

| Ethernet twisted pair | Signal            | OBD-II connector pin
| --------------------- | ----------------- | -
| blue + white blue     | +12V              | 16
| brown + white brown   | ground            | 4
| white orange          | HS CANH (500kbps) | 6
| orange                | HS CANL (500kbps) | 14
| white green           | MS CANH (125kbps) | 3
| green                 | MS CANL (125kbps) | 11

### SD card pin mapping

| Card pin | SPI mode | SD 1-bit mode | SD 4-bit mode | ESP32
| -------- | -------- | ------------- | ------------- | -
| 1        | -        | -             | DAT2          | (12)
| 2        | CS       | CD            | DAT3          | (13)
| 3        | MOSI     | CMD           | CMD           | 15
| 4        | 3V3      | 3V3           | 3V3           | 3V3
| 5        | SCLK     | CLK           | CLK           | 14
| 6        | GND      | GND           | GND           | GND
| 7        | MISO     | DAT0          | DAT0          | 2
| 8        | -        | -             | DAT1          | (4)

Notes:

- For SD 1-bit mode, CMD, CLK and DAT0 lines must be pulled up (10KOhm). CD/DAT3 should also be pulled up (10KOhm) on the card side
- GPIO2 (DAT0) conflicts with ESP32 serial download mode, possible solutions: disconnect GPIO2 from SD or jumper GPIO0 and GPIO2 (auto-reset should work)

Schematic of the microSD breakout board I'm using:

![schematic](https://win.adrirobot.it/Micro_SD_Card_Module/Micro-SD-Card-Module_circuit.jpg)

### Useful links

- http://opengarages.org/handbook/ebook/
- https://forscan.org/forum/viewtopic.php?t=4

#### OBD-II

- https://en.wikipedia.org/wiki/OBD-II_PIDs
- https://x-engineer.org/automotive-engineering/internal-combustion-engines/diagnostics/on-board-diagnostics-obd-modes-operation-diagnostic-services/
- https://www.mazdaclub.it/forum/viewtopic.php?f=52&t=23307&p=272187&hilit=extra#p272187

#### Mazda manufacturer messages

- http://she-devel.com/Mazda3_Controller_Area_Network_Experimentation.html
- http://www.madox.net/blog/projects/mazda-can-bus/
- https://github.com/majbthrd/MazdaCANbus/blob/master/skyactiv.kcd
- http://opengarages.org/index.php/Mazda_CAN_ID

#### Linux SocketCAN

- https://www.kernel.org/doc/Documentation/networking/can.txt
- https://elinux.org/CAN_Bus
- https://elinux.org/Bringing_CAN_interface_up
- https://github.com/linux-can/can-utils
- https://python-can.readthedocs.io/en/master/interfaces/slcan.html
- https://github.com/torvalds/linux/blob/master/drivers/net/can/slcan.c
- https://github.com/darauble/lawicel-slcan
- http://www.can232.com/docs/can232_v3.pdf
- http://www.can232.com/docs/canusb_manual.pdf
