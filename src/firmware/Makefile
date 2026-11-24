NASM ?= nasm
LD ?= ld
OBJCOPY ?= objcopy

BIN_DIR = ../bin
TARGET = $(BIN_DIR)/firmware.bin
OBJECTS = $(BIN_DIR)/firmware.o
ELF = $(BIN_DIR)/firmware.elf

.PHONY: all clean

all: $(TARGET)

$(OBJECTS): firmware.asm
	mkdir -p $(BIN_DIR)
	$(NASM) -f elf32 $< -o $(OBJECTS)

$(ELF): $(OBJECTS) firmware.ld
	mkdir -p $(@D)
	$(LD) -m elf_i386 -T firmware.ld -o $@ $(OBJECTS)

$(TARGET): $(ELF)
	mkdir -p $(@D)
	$(OBJCOPY) -O binary --gap-fill 0xFF $< $@
	truncate -s 65536 $@

clean:
	rm -f $(OBJECTS) $(ELF) $(TARGET)
