About
-----

The thing started when I took over the coding of regulation of heating at my home. This includes wood boiler, solar collectors, two hot water tanks, radiators and includes HTML web control interface.
It is based on AVR uC. There are 10 1-Wire temperature sensors, 4 valves, and 4 pumps. uC is then connected to PC, where CherryPy server is running and directly communicating with uC. The best part about web interface is that it is automatically generated based on C code. Info about variables and functions is extracted from object file produced by GCC and you can automatically control and call them via web interface. Use you imagination to see how this can ease debugging and building of the final interface.

It was tested on custom made system which I use for regulation and Arduino for one other task. I don't see a reason why at least parts of the code could be used for other uCs.

Design
------

This is how general idea looks like:
```
        Control interface             |   Autonomous base functionality
                                      |
*---------------------------------*   |     
| PC                              |   |     
| *--------*      *-------------* |   |   *----*       
| | Web    | ---> | Python glue | |   |   |    | <- sensors (thermal)
| | server | <--- | logic       | | <===> | uC |
| *--------*      *-------------* |   |   |    | -> devices (valves, pumps)
|                         /\      |   |   *----*
|                         ||      |   |     ^
|                      [ BINFO ]  |   |     ^
*-------------------------^-------*   |     ^
                          ^           |     ^
 Run-time                 ^           |     ^
--------------------------^-----------------^---------------------------------
 Compile-time             ^                 ^
                    *------------*          ^
                    |DWARF parser|          ^
          *------*  *------------*          ^
          | uC   |        ^                 ^
          | code | ---> [elf]  ---------> [bin]
          | base |
          *------*
```

One important block here is BINFO. It consists of data gathered while compiling
uC source code:
 - globals
 - functions
 - macros
 - types (TODO)
 - function parameters & return value (TODO)
 - function registers, stack values, stack depth (TODO)

What it is that good for? To automatically generate control interface. E.g.
global configuration variable named "goal_temp" can be written to set the
desired heating temperature. Or function get_temp(sensor_nr) can be triggered
with to read a specific sensor current temperature. Just create a variable or
function and it can be get/set or executed via control interface.
However, there is one thing lacking here to make this even better - types.

This is roughly what is currently done:

```
uC source code -> GCC --> elf --> uC binary
                              `-> objdump ->BINFO
```

To get info about types and other useful stuff, DWARF debug info is the best
way to go as of my knowledge. However, it seems like a major task so any ideas
how to achieve that as fast as possible are welcome.
This is what I consider as the most important next step. Most of debugging is
done via this interface using knowledge included in BINFO. So this will not
only affect control interface, but also heavily ease debugging.


These are features that are currently supported on uC:
- bootloader (update-able)
- scheduler (no priorities yet)
- loops manager 
- timer (including 32 timer queue implemented)
- RTC
- cron
- non-volatile configuration (flash)
- watchdog
- stack checking

External:
- PIO abstraction
- 1-Wire DS18B20 temperature support
- relays
- valves
- UART (to PC)

And some stuff that is relevant for my specific setup:
- solar collectors control (pump)
- furnace control (valve, pump)
- house heating (valve, pump)
- 2 hot water storage exchange (2 valves, pump)
- LCD


Code (especially Python part) might not be very pretty, but it serves me well
for at least a year.



Getting Started
---------------

*Beta - contact when you stuck*

You need:
- Arduino board
- avr-gcc toolchain with avr-libc (I use Gentoo with crossdev)
- avrdude
- pyserial
- cherrypy

Compile the code:

    ./make.py

Flash the flash (ideally you would do that only once, cause there is a web based bootloader available):

    avrdude -c stk500v2 -P /dev/ttyUSB0 -p m328p  -U flash:w:fw.bin

Set the fuses:

    ./py/fuse.py restore

Start the server:

    ./py/server.py

Connect to localhost:8000. I suggest you visit the flash page first and apply update.tar.bz2 created by make, cause bootloader was not updated by avrdude.


Contributing
------------

Is very welcome and that's why this project is here. If I would have all the time on the world I would code everything myself and I would be rich.

License
-------

GPL
