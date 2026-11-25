# Builds a program: make APP=examples/snake NAME=snake TITLE=Snake  (Linux or WSL with gcc, binutils, python3)
APP ?= examples/hello
NAME ?= $(notdir $(APP))
TITLE ?= $(NAME)
OUT ?= build
CFLAGS = -O2 -ffreestanding -fno-stack-protector -fpie -fno-asynchronous-unwind-tables -nostdinc -isystem $(shell gcc -print-file-name=include) -m64 -march=x86-64 -I. -fno-tree-loop-distribute-patterns $(EXTRA)
# LIBSRC: more C files (a library) built with LIBFLAGS and linked in, LIBOBJ their objects
LIBSRC ?=
LIBOBJ = $(foreach f,$(LIBSRC),$(OUT)/lib-$(NAME)/$(notdir $(f:.c=.o)))
$(OUT)/$(NAME).vpk: $(APP).c $(wildcard examples/*.h) violyx.c violyx.h link.ld mkvpk.py $(LIBSRC)
	@mkdir -p $(OUT) $(if $(LIBSRC),$(OUT)/lib-$(NAME))
	gcc $(CFLAGS) -c $(APP).c -o $(OUT)/$(NAME).o
	gcc $(CFLAGS) -c violyx.c -o $(OUT)/violyx.o
	$(foreach f,$(LIBSRC),gcc $(CFLAGS) $(LIBFLAGS) -c $(f) -o $(OUT)/lib-$(NAME)/$(notdir $(f:.c=.o)) &&) true
	ld -m elf_x86_64 -static -nostdlib -z max-page-size=0x1000 -z noexecstack -T link.ld -o $(OUT)/$(NAME).elf $(OUT)/$(NAME).o $(OUT)/violyx.o $(LIBOBJ)
	python3 mkvpk.py $(OUT)/$(NAME).elf $@ --name $(NAME) --title "$(TITLE)" $(VPKFLAGS)

# The Vorbis encoder of Xiph.org for XZN's OGG rips (decoding stays with stb_vorbis)
VB = ../third_party/vorbis/lib
VORBIS = helpers/vxogg.c ../third_party/ogg/src/bitwise.c ../third_party/ogg/src/framing.c $(addprefix $(VB)/,analysis.c bitrate.c block.c codebook.c envelope.c floor0.c floor1.c info.c lpc.c lsp.c mapping0.c mdct.c psy.c registry.c res0.c sharedbook.c smallft.c vorbisenc.c window.c synthesis.c)
# Builds all examples into build/
apps:
	$(MAKE) APP=examples/hello NAME=hello TITLE=Hello VPKFLAGS='--cols 60 --rows 12 --author Merecete --icon "      | Hi!  |      " --icon-png icons/hello.png'
	$(MAKE) APP=examples/snake NAME=snake TITLE=Snake VPKFLAGS='--cols 60 --rows 19 --version 1.1 --author Merecete --icon " ~~~~ |  ~~@ |      " --icon-png icons/snake.png'
	$(MAKE) APP=examples/bricks NAME=bricks TITLE=Bricks VPKFLAGS='--cols 60 --rows 19 --version 1.1 --author Merecete --icon "##  ##|  ##  |  O   " --icon-png icons/bricks.png'
	$(MAKE) APP=examples/novastrike NAME=novastrike TITLE=Novastrike VPKFLAGS='--cols 60 --rows 19 --author Merecete --icon "  V   |  :   | /A\\ " --icon-png icons/novastrike.png'
	$(MAKE) APP=examples/gemfall NAME=gemfall TITLE=Gemfall VPKFLAGS='--cols 60 --rows 19 --author Merecete --icon " <> o |  # <>| o## # " --icon-png icons/gemfall.png'
	$(MAKE) APP=examples/vioisland NAME=vioisland TITLE=Vio-Island EXTRA='-Ihelpers/inc' VPKFLAGS='--cols 76 --rows 20 --author Merecete --icon " \\|/  |  |~~~|~~|~~~" --icon-png icons/vioisland.png'
	$(MAKE) APP=examples/fetch NAME=fetch TITLE=Fetch VPKFLAGS='--cols 70 --rows 18 --author Merecete --icon " .--. |( () )| '"'"'--'"'"' " --icon-png icons/fetch.png'
	$(MAKE) APP=examples/starfield NAME=starfield TITLE=Sternenflug VPKFLAGS='--saver --author Merecete'
	$(MAKE) APP=examples/plasma NAME=plasma TITLE=Plasma VPKFLAGS='--saver --author Merecete'
	$(MAKE) APP=helpers/vxdec NAME=vxdec TITLE=vxdec EXTRA='-Ihelpers/inc -I../third_party/stb'
	$(MAKE) APP=examples/xzn NAME=xzn TITLE=XZN EXTRA='-Ihelpers/inc -I../third_party/stb -I../third_party/minimp3 -I../third_party/dr_flac -I../third_party/ogg/include -I../third_party/vorbis/include' LIBSRC='$(VORBIS)' LIBFLAGS='-w -I../third_party/vorbis/lib -include helpers/vxogg.h' VPKFLAGS='--cols 76 --rows 20 --author Merecete --version 1.3 --icon "\\\\/ ~~ |/\\\\ ~~~|  ~~  " --icon-png icons/xzn.png'
	$(MAKE) APP=examples/vioaudio NAME=vioaudio TITLE=Vio-Audio EXTRA='-Ihelpers/inc -I../third_party/stb -I../third_party/minimp3 -I../third_party/dr_flac -I../third_party/ogg/include -I../third_party/vorbis/include' LIBSRC='$(VORBIS)' LIBFLAGS='-w -I../third_party/vorbis/lib -include helpers/vxogg.h' VPKFLAGS='--cols 76 --rows 20 --author Merecete --icon " ▐▀▀▀▌| ▐   ▌|██ ██▌" --icon-png icons/vioaudio.png'
