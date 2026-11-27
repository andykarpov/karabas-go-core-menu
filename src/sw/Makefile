# The program of the menu CPU (rtl/soc/menu_soc.v): menu.bin is the ROM of boot.kg (core.json roms).
XPACK  ?= $(HOME)/opt/xpack-riscv-none-elf-gcc-15.2.0-1/bin
CROSS  := $(XPACK)/riscv-none-elf-
CFLAGS := -Ilib -march=rv32im -mabi=ilp32 -O2 -g -ffreestanding -fno-builtin -ffunction-sections -fdata-sections -fno-tree-loop-distribute-patterns -Wall
B      := build

all: ../roms/menu.bin

# the ROM of boot.kg (core.json roms, loaded to address 0 of the SRAM)
../roms/menu.bin: $(B)/menu.bin
	mkdir -p ../roms
	cp $< $@

$(B)/%.o: %.c $(wildcard *.h) $(wildcard lib/*.h) | $(B)
	mkdir -p $(dir $@)
	$(CROSS)gcc $(CFLAGS) -c $< -o $@
$(B)/%.o: %.S | $(B)
	$(CROSS)gcc $(CFLAGS) -c $< -o $@
SRCS   := main.c intro.c fx.c show.c ui.c fonts.c settings.c browser.c groups.c setup.c rpc.c gfx.c sound.c syscalls.c esp.c wifi.c wifi_page.c pad_page.c esp_update.c ntp.c \
          lib/esp_at_parse.c lib/time_zone.c lib/masked_input.c
OBJS   := $(B)/crt0.o $(addprefix $(B)/,$(SRCS:.c=.o))
$(B)/menu.elf: $(OBJS) link.ld
	$(CROSS)gcc $(CFLAGS) -nostdlib -nostartfiles -T link.ld -Wl,--gc-sections -o $@ $(OBJS) -lc_nano -lgcc
	$(CROSS)size $@
$(B)/menu.bin: $(B)/menu.elf
	$(CROSS)objcopy -O binary $< $@
$(B):
	mkdir -p $(B)
# the generated sources (committed): fonts from Noto Sans, the pictures of the firmware
fonts:
	python3 tools/mkfonts.py fonts_gen.h
images:
	python3 tools/mkimages.py assets/ft_bitmaps.h images_gen.h
clean:
	rm -rf $(B)
.PHONY: all clean fonts images
