LIMINE ?= third_party/limine
FONT   ?= third_party/spleen/spleen-8x16-ibm-437.bdf
CC      = gcc
CFLAGS  = -U__linux__ -U__unix__ -U__unix -Ulinux -Uunix -nostdinc -isystem $(shell gcc -print-file-name=include) -m64 -ffreestanding -fno-pic -fno-pie -fno-stack-protector -fno-builtin -mno-red-zone \
          -mcmodel=kernel -mgeneral-regs-only -O2 -Wall -Wextra -Wno-unused-parameter -Wno-misleading-indentation -Wno-missing-field-initializers -std=gnu11 $(EXTRA)
QEMU   ?= qemu-system-x86_64
LW      = third_party/lwip/src
LWSRC   = $(wildcard $(LW)/core/*.c $(LW)/core/ipv4/*.c) $(LW)/netif/ethernet.c
LWOBJ   = $(patsubst $(LW)/%.c,build/lw/%.o,$(LWSRC))
MB      = third_party/mbedtls
MBSRC   = $(wildcard $(MB)/library/*.c)
MBOBJ   = $(patsubst $(MB)/library/%.c,build/mb/%.o,$(MBSRC))
CACERTS ?= third_party/cacert/cacert.pem
INC     = -Ikernel/lwip_port -Ikernel/libc -I$(LW)/include -I$(MB)/include -Ikernel/mbed_port -DMBEDTLS_CONFIG_FILE='"violyx_tls_config.h"'
OBJ     = build/entry.o build/main.o build/kbd.o build/lib.o build/disk.o build/ui.o build/apps.o build/pci.o build/ahci.o build/nvme.o build/xhci.o build/acpi.o build/net.o build/tls.o build/http.o build/html.o build/browser.o build/install.o build/viofs.o build/fat.o build/drives.o build/games.o build/calendar.o build/prog.o build/rge.o $(LWOBJ) $(MBOBJ)
all: build/violyx.img build/violyx.iso build/violyx.vdi
build/font.h: tools/mkfont.py
	@mkdir -p build
	python3 tools/mkfont.py $(FONT) $@
build/ui.o: build/font.h
build/ca_der.h: tools/mkca.py $(CACERTS)
	@mkdir -p build
	python3 tools/mkca.py $@ $(CACERTS)
build/tls.o: build/ca_der.h
build/cp437.h: tools/mkcp437.py
	@mkdir -p build
	python3 tools/mkcp437.py $@
build/html.o: build/cp437.h kernel/html.h
build/fat.o: build/cp437.h kernel/fat.h kernel/viofs.h
build/drives.o: kernel/fat.h kernel/viofs.h
build/browser.o: kernel/html.h
build/http.o: kernel/html.h
build/mb/%.o: $(MB)/library/%.c kernel/mbed_port/violyx_tls_config.h
	@mkdir -p build/mb
	$(CC) $(CFLAGS) -w $(INC) -c $< -o $@
build/%.o: kernel/%.c kernel/os.h
	@mkdir -p build
	$(CC) $(CFLAGS) -Ibuild $(INC) -c $< -o $@
build/lw/%.o: $(LW)/%.c kernel/lwip_port/lwipopts.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -w $(INC) -c $< -o $@
build/entry.o: kernel/entry.asm
	@mkdir -p build
	nasm -f elf64 $< -o $@
build/violyx.elf: $(OBJ) kernel/link.ld
	ld -m elf_x86_64 -nostdlib -static -z max-page-size=0x1000 -T kernel/link.ld -o $@ $(OBJ)
build/base.img: build/violyx.elf tools/mkimage.py limine-base.conf
	python3 tools/mkimage.py build/violyx.elf $(LIMINE) limine-base.conf $@
	$(MAKE) -C $(LIMINE) limine >/dev/null
	$(LIMINE)/limine bios-install $@ --quiet
build/install.bin: build/base.img tools/mksparse.py
	python3 tools/mksparse.py $< $@
build/violyx.img: build/violyx.elf build/install.bin tools/mkimage.py limine.conf
	python3 tools/mkimage.py build/violyx.elf $(LIMINE) limine.conf $@ build/install.bin
	$(LIMINE)/limine bios-install $@ --quiet
build/violyx.iso: build/violyx.elf build/install.bin tools/mkiso.py limine.conf
	python3 tools/mkiso.py build/violyx.elf $(LIMINE) limine.conf $@ build/install.bin
build/violyx.vdi: build/violyx.img tools/mkvdi.py
	python3 tools/mkvdi.py $< $@
run: all
	$(QEMU) -drive file=build/violyx.img,format=raw -m 256
clean:
	rm -rf build
.PHONY: all run clean
