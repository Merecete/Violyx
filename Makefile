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
OBJ     = build/entry.o build/main.o build/kbd.o build/lib.o build/str.o build/disk.o build/ui.o build/apps.o build/pci.o build/ahci.o build/nvme.o build/xhci.o build/acpi.o build/net.o build/sound.o build/install.o build/viofs.o build/vfsname.o build/fat.o build/exfat.o build/format.o build/drives.o build/clock.o build/saver.o build/prog.o build/rge.o build/wall.o build/efi.o $(LWOBJ)
all: build/violyx.img build/violyx.iso build/violyx.vdi
build/font.h: tools/mkfont.py
	@mkdir -p build
	python3 tools/mkfont.py $(FONT) $@
build/ui.o: build/font.h
build/ca_der.h: tools/mkca.py $(CACERTS)
	@mkdir -p build
	python3 tools/mkca.py $@ $(CACERTS)
build/cp437.h: tools/mkcp437.py
	@mkdir -p build
	python3 tools/mkcp437.py $@
APPSRC=$(wildcard sdk/examples/*.c sdk/helpers/*.c sdk/*.h) sdk/violyx.c sdk/violyx.h sdk/link.ld sdk/mkvpk.py sdk/Makefile
build/apps/sdk.stamp: $(APPSRC)
	$(MAKE) -C sdk apps OUT=../build/apps
	touch $@

# ---------- System programs in ring 3 (system/): built like SDK programs, embedded in the kernel ----------
U3      = -U__linux__ -U__unix__ -U__unix -Ulinux -Uunix -O2 -ffreestanding -fno-stack-protector -fpie -fno-asynchronous-unwind-tables -nostdinc -isystem $(shell gcc -print-file-name=include) -m64 -march=x86-64 -fno-tree-loop-distribute-patterns -DRING3 -Isystem -Isystem/web -Ikernel -Ibuild \
          -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Wno-sign-compare -Wno-misleading-indentation -std=gnu11
UMB     = -Ikernel/libc -I$(MB)/include -Ikernel/mbed_port -DMBEDTLS_CONFIG_FILE='"violyx_tls_config.h"'
UHDR    = system/os.h system/common.h kernel/str.h kernel/viofs.h kernel/version.h
UMBOBJ  = $(patsubst $(MB)/library/%.c,build/umb/%.o,$(MBSRC))
URT     = build/u/rt.o build/u/str.o build/u/vfsname.o
UWEB    = build/u/web/http.o build/u/web/tls.o build/u/web/cookies.o build/u/web/html.o build/u/decode.o build/umb/libmbed.a
ULD     = ld -m elf_x86_64 -static -nostdlib --no-relax -z max-page-size=0x1000 -z noexecstack -T sdk/link.ld
build/u/%.o: system/%.c $(UHDR)
	@mkdir -p $(dir $@)
	$(CC) $(U3) -c $< -o $@
build/u/str.o: kernel/str.c kernel/str.h
	@mkdir -p build/u
	$(CC) $(U3) -c $< -o $@
build/u/vfsname.o: kernel/vfsname.c kernel/viofs.h
	@mkdir -p build/u
	$(CC) $(U3) -c $< -o $@
build/u/decode.o: system/decode.c sdk/helpers/vxdec.c $(UHDR)
	@mkdir -p build/u
	$(CC) $(U3) -Isdk/helpers/inc -Ithird_party/stb -w -c $< -o $@
build/u/web/%.o: system/web/%.c system/web/web.h system/web/html.h $(UHDR)
	@mkdir -p build/u/web
	$(CC) $(U3) $(UMB) -c $< -o $@
build/u/web/tls.o: build/ca_der.h
build/u/web/html.o: build/cp437.h
build/umb/libmbed.a: $(UMBOBJ)
	rm -f $@
	ar rcs $@ $^
build/umb/%.o: $(MB)/library/%.c kernel/mbed_port/violyx_tls_config.h
	@mkdir -p build/umb
	$(CC) $(U3) -w $(UMB) -c $< -o $@
build/u/games_%.o: system/games.c $(UHDR)
	@mkdir -p build/u
	$(CC) $(U3) -DGAME=app_$* -c $< -o $@
build/u/clock.o: kernel/clockface.h kernel/sintab.h
build/u/commands.o: system/web/web.h
# name, title, columns and rows of each program
VPK_calc     = --name calc --title Rechner --cols 40 --rows 10
VPK_calendar = --name calendar --title Kalender --cols 64 --rows 17
VPK_clock    = --name clock --title Uhr --cols 30 --rows 16
VPK_write    = --name write --title Schreiben --cols 60 --rows 15
VPK_view     = --name view --title Bildbetrachter --cols 42 --rows 7
VPK_paint    = --name paint --title Malprogramm --cols 60 --rows 18
VPK_files    = --name files --title Dateien --cols 62 --rows 16
VPK_commands = --name commands --title Befehle --cols 62 --rows 13
VPK_web      = --name web --title "Violyx Internet" --cols 76 --rows 20
VPK_pong     = --name pong --title Pong --cols 64 --rows 18
VPK_invaders = --name invaders --title Invaders --cols 64 --rows 20
VPK_chomp    = --name chomp --title Chomp --cols 42 --rows 20
VPK_mines    = --name mines --title Mines --cols 50 --rows 17
SYSPROG = calc calendar clock write view paint files commands web pong invaders chomp mines
build/sys/calc.elf: build/u/calc.o $(URT)
build/sys/calendar.elf: build/u/calendar.o $(URT)
build/sys/clock.elf: build/u/clock.o $(URT)
build/sys/write.elf: build/u/write.o $(URT)
build/sys/view.elf: build/u/viewer.o build/u/decode.o $(URT)
build/sys/paint.elf: build/u/paint.o $(URT)
build/sys/files.elf: build/u/files.o $(URT)
build/sys/commands.elf: build/u/commands.o $(UWEB) $(URT)
build/sys/web.elf: build/u/web/browser.o $(UWEB) $(URT)
build/sys/pong.elf: build/u/pong.o $(URT)
build/sys/invaders.elf: build/u/games_invaders.o $(URT)
build/sys/chomp.elf: build/u/games_chomp.o $(URT)
build/sys/mines.elf: build/u/games_mines.o $(URT)
build/sys/%.elf: sdk/link.ld
	@mkdir -p build/sys
	$(ULD) -o $@ $(filter %.o,$^) $(filter %.a,$^)
build/sys/%.vpk: build/sys/%.elf sdk/mkvpk.py
	python3 sdk/mkvpk.py $< $@ $(VPK_$*) --author Merecete --version $(shell sed -n 's/.*"\(.*\)".*/\1/p' kernel/version.h)
build/apps.h: build/apps/sdk.stamp $(patsubst %,build/sys/%.vpk,$(SYSPROG)) tools/mkapps.py
	python3 tools/mkapps.py $@ snake=build/apps/snake.vpk bricks=build/apps/bricks.vpk novastrike=build/apps/novastrike.vpk xzn=build/apps/xzn.vpk $(foreach p,$(SYSPROG),$(p)=build/sys/$(p).vpk)
build/prog.o: build/apps.h
build/upcase.h: tools/mkupcase.py
	@mkdir -p build
	python3 tools/mkupcase.py $@
build/fat.o: build/cp437.h kernel/fat.h kernel/viofs.h
build/exfat.o: build/cp437.h kernel/exfat.h kernel/viofs.h
build/format.o: build/cp437.h build/upcase.h kernel/format.h kernel/viofs.h
build/drives.o: kernel/fat.h kernel/exfat.h kernel/format.h kernel/viofs.h
build/clock.o: kernel/clockface.h kernel/sintab.h
build/%.o: kernel/%.c kernel/os.h kernel/version.h kernel/str.h
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
