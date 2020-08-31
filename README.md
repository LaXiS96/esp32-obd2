# ESP32 ODB-II adapter

This project starts as a tool to access the CAN busses on my personal Mazda3, for reverse engineering and for fun.

## Features
- CAN connection with vehicle (using any compatible transceiver: SN65HVD23x, MCP2551 (with RX voltage translation), etc...)
- Linux SocketCAN compatibility (to allow usage of the very useful can-utils) via serial interface (slcan driver)
- socket communication over WiFi softAP (TBD)
- custom protocol for better efficiency (TBD)
- reverse engineered Mazda-specific messages (TODO)
- OBD diagnostics (TODO)
- ... (TBD)

## Useful info

### Linux SocketCAN / can-utils usage example

This adapter will implement the LAWICEL SLCAN protocol as expected by the slcan SocketCAN driver, so that it can be used with `slcand` and allow the usage of can-utils programs like `cansniffer`.

    sudo slcand -o -c -f -S 576000 /dev/ttyUSB0 slcan0
    sudo ip link set up slcan0
    cansniffer slcan0

### Cable

OBD-II connector pins:

    3   MS CANH
    4   chassis ground
    6   HS CANH
    11  MS CANL
    14  HS CANL
    16  +12V

Ethernet cable mapping (similar to Ethernet 10/100 PoE):

    blue + white blue       +12V    (16)
    brown + white brown     ground  (4)
    white orange            HS CANH (6)
    orange                  HS CANL (14)
    white green             MS CANH (3)
    green                   MS CANL (11)

### Data rates

HS CAN: 500kbps

MS CAN: 125kbps

### Links

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
