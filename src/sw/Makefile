# The program of the menu CPU (rtl/soc/menu_soc.v): menu.bin is the ROM of boot.kg (core.json roms).
XPACK  ?= $(HOME)/opt/xpack-riscv-none-elf-gcc-15.2.0-1/bin
CROSS  := $(XPACK)/riscv-none-elf-
CFLAGS := -march=rv32im -mabi=ilp32 -O2 -g -ffreestanding -fno-builtin -ffunction-sections -fdata-sections -Wall
B      := build

all: ../roms/menu.bin

# the ROM of boot.kg (core.json roms, loaded to address 0 of the SRAM)
../roms/menu.bin: $(B)/menu.bin
	mkdir -p ../roms
	cp $< $@

$(B)/%.o: %.c mmio.h font8.h | $(B)
	$(CROSS)gcc $(CFLAGS) -c $< -o $@
$(B)/%.o: %.S | $(B)
	$(CROSS)gcc $(CFLAGS) -c $< -o $@
$(B)/menu.elf: $(B)/crt0.o $(B)/main.o link.ld
	$(CROSS)gcc $(CFLAGS) -nostdlib -nostartfiles -T link.ld -Wl,--gc-sections -o $@ $(B)/crt0.o $(B)/main.o -lgcc
	$(CROSS)size $@
$(B)/menu.bin: $(B)/menu.elf
	$(CROSS)objcopy -O binary $< $@
$(B):
	mkdir -p $(B)
clean:
	rm -rf $(B)
.PHONY: all clean
