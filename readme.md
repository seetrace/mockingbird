# Mockingbird

by Yuta Seiya

## Usage

```sh
./bin/emulate <firmware.bin> [disk.img]
```

The disk image is opened read/write. It may be empty and can grow sparsely as
the guest writes to new sectors.

The firmware includes real-mode reset-vector setup and BIOS interrupt services:

- INT 10h: text output and cursor/mode queries
- INT 13h: fixed-disk reset, read, write, and geometry
- INT 16h: keyboard input and status
- INT 18h/19h: diskless boot and reset/boot services

Mockingbird is a indie project where a fully custom hypervisor built from the ground up based on the orginal IBM PC era

## Build

**Build the actual hypervisor**

cd ./src

make

When finished the binary gets put in ./src/bin/emulate

**Build BIOS/Guest firmware**

cd ./src/firmware

make

When finished the binary get put in ./src/bin/firmware.bin

Note you can bring your own firmware but i *cannot* verify your firmware is compateble with mockingbird

## featues & deveolopment

Teletype Output (Supported)

Teletype Input (Supported)

Firmware (Supported)

Custom Firmware (Supported)

input disk a (Supported)

Kernel Support (Unsupported) (Linux kernel support)

input disk b (Unsupported)



custom memory allocation (Unsupported & not planned)

custom core allocation (Unsuppored & not planned)

mouse support (Unsupported & not planned)

networking device (Unsupported & not planned)

VGA screen (Unsuppored & not planned)
