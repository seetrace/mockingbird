CC = gcc

BIN_DIR = bin
TARGET = $(BIN_DIR)/emulate

CFLAGS = -D_GNU_SOURCE -Wall -Wextra -std=c11 -O2
CPPFLAGS = -Icpu

SRCS = \
	main.c \
	guest/guest.c \
	guest/memory.c \
	cpu/vcpu.c \
	kvm/kvm.c \
	devices/serial.c \
	devices/disk.c

OBJS = $(patsubst %.c,$(BIN_DIR)/%.o,$(SRCS))

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJS)
	mkdir -p $(@D)
	$(CC) $(OBJS) -o $(TARGET)

$(BIN_DIR)/%.o: %.c
	mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

run: $(TARGET)
	$(TARGET)