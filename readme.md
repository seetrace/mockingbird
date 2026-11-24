# Mockingbird

by Yuta Seiya

Mockingbird is a indie project where a fully custom hypervisor built from the ground up based on the orginal IBM PC era

## Usage

./emulate <firmware>

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

Teletype (Partial support, Output only)

Firmware (Supported)

Custom Firmware (Supported)

input disk a (Unsupported)

input disk b (Unsupported)


custom memory allocation (Unsupported & not planned)

custom core allocation (Unsuppored & not planned)

mouse support (Unsupported & not planned)

networking device (Unsupported & not planned)

VGA screen (Unsuppored & not planned)