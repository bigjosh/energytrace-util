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
# Usage: make windows-x86  or  make windows-x64
MSP430_WIN_INCLUDE ?= Inc
windows-x86: $(SRC) MSP430.def
	i686-w64-mingw32-gcc -I$(MSP430_WIN_INCLUDE) -o $(TARGET)-x86.exe $< MSP430.def
windows-x64: $(SRC) MSP430.def
	x86_64-w64-mingw32-gcc -I$(MSP430_WIN_INCLUDE) -o $(TARGET)-x64.exe $< MSP430.def
windows: windows-x86 windows-x64
