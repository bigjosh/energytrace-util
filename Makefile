TARGET=energytrace
SRC = $(TARGET).c

CFLAGS = -IInc -lmsp430

all: $(TARGET)
$(TARGET): $(SRC)
	gcc $(CFLAGS) -o $@ $<
clean:
	rm -f $(TARGET) $(TARGET).exe

run: all
	./energytrace 5

install: all
	install -t /usr/local/bin $(TARGET)

# Cross-compile for Windows (requires mingw-w64 and MSP430 headers)
# Usage: make windows MSP430_WIN_INCLUDE=/path/to/msp430/include
MINGW_CC = i686-w64-mingw32-gcc
MSP430_WIN_INCLUDE ?= Inc
windows: $(SRC) MSP430.def
	$(MINGW_CC) -I$(MSP430_WIN_INCLUDE) -o $(TARGET).exe $< MSP430.def
