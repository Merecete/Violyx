# Baut ein Programm: make APP=examples/snake NAME=snake TITLE=Snake  (Linux oder WSL mit gcc, binutils, python3)
APP ?= examples/hello
NAME ?= $(notdir $(APP))
TITLE ?= $(NAME)
OUT ?= build
CFLAGS = -O2 -ffreestanding -fno-stack-protector -fpie -fno-asynchronous-unwind-tables -nostdinc -isystem $(shell gcc -print-file-name=include) -m64 -march=x86-64 -I. -fno-tree-loop-distribute-patterns $(EXTRA)
$(OUT)/$(NAME).vpk: $(APP).c violyx.c violyx.h link.ld mkvpk.py
	@mkdir -p $(OUT)
	gcc $(CFLAGS) -c $(APP).c -o $(OUT)/$(NAME).o
	gcc $(CFLAGS) -c violyx.c -o $(OUT)/violyx.o
	ld -m elf_x86_64 -static -nostdlib -z max-page-size=0x1000 -z noexecstack -T link.ld -o $(OUT)/$(NAME).elf $(OUT)/$(NAME).o $(OUT)/violyx.o
	python3 mkvpk.py $(OUT)/$(NAME).elf $@ --name $(NAME) --title "$(TITLE)" $(VPKFLAGS)

# Baut alle Beispiele nach build/
apps:
	$(MAKE) APP=examples/hello NAME=hello TITLE=Hello VPKFLAGS='--cols 60 --rows 12 --author Merecete --icon "      | Hi!  |      "'
	$(MAKE) APP=examples/snake NAME=snake TITLE=Snake VPKFLAGS='--cols 60 --rows 19 --version 1.1 --author Merecete --icon " ~~~~ |  ~~@ |      "'
	$(MAKE) APP=examples/bricks NAME=bricks TITLE=Bricks VPKFLAGS='--cols 60 --rows 19 --author Merecete --icon "##  ##|  ##  |  O   "'
	$(MAKE) APP=examples/novastrike NAME=novastrike TITLE=Novastrike VPKFLAGS='--cols 60 --rows 19 --author Merecete --icon "  V   |  :   | /A\\ "'
	$(MAKE) APP=helpers/vxdec NAME=vxdec TITLE=vxdec EXTRA='-Ihelpers/inc -I../third_party/stb'
	$(MAKE) APP=examples/xzn NAME=xzn TITLE=XZN EXTRA='-Ihelpers/inc -I../third_party/stb -I../third_party/minimp3 -I../third_party/dr_flac' VPKFLAGS='--cols 76 --rows 20 --author Merecete --version 1.0 --icon "\\\\/ ~~ |/\\\\ ~~~|  ~~  "'
