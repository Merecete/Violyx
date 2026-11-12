# Violyx

**A keyboard-only 64-bit operating system for x86 PCs, with a retro text desktop in glowing violet.**

Violyx is written from scratch, with its own kernel, drivers, file system and programs. There is no
Linux underneath. It boots on BIOS and UEFI, goes online, plays music and games, all without a
mouse.

This project was created with the help of AI. It is written in C and x86-64 assembly, Python is
used for the build tools.

![Violyx boot logo](docs/logo.png)

| | |
|---|---|
| ![Programs](docs/programs.png) | ![Browser](docs/browser.png) |
| ![XZN](docs/XZN-Violyx.png) | ![Games](docs/games.png) |

| | | |
|---|---|---|
| ![Violet](docs/violyx-violet.png) | ![Ruby](docs/violyx-ruby.png) | ![Ocean](docs/violyx-ocean.png) |

> Experimental hobby project, version 0.2.1. Please do not enter passwords or use it for anything
> sensitive.

## What's inside

- Windowed text desktop, controlled entirely by keyboard, with colour schemes and screen savers
- Web browser with HTTPS, images, tabs, bookmarks and downloads
- Write, Files, Commands, Calculator, Calendar, Clock, Image viewer and Paint
- XZN music player for MP3, OGG, FLAC, WAV, AIFF, WavPack, DSD, AC3 and more
- Games: Pong (also for two players), Invaders, Chomp, Mines, Snake, Bricks and Novastrike
- Every program runs in its own protected memory, so a crash only ends that program
- Sound, wired network, USB sticks and SD cards (FAT32 and exFAT), CDs and DVDs, English and German
- Installer for the entire disk or next to Windows or another system

### Programming

- Built-in C compiler: write a program in Write and compile it in Commands with `cc`, right on Violyx
- [SDK](sdk/README.md) in C with one header, `violyx.h`: text grid with 256 colours, keyboard,
  files, dialogs, network and sound
- Programs are shared as `.vpk` packages and installed with one key press in Files
- The examples, including Snake, Novastrike and the XZN player, come with full source code

## Try it

Download `violyx_v0.2.1.iso` from the [Releases](../../releases) page.

**VirtualBox:** create a VM with type *Other/Unknown (64-bit)* and 512 MB memory, and start it from
the ISO. For internet, choose the network adapter *Intel PRO/1000 MT Desktop*. For sound, enable
the *Intel HD Audio* controller.

**QEMU:**

```sh
qemu-system-x86_64 -cdrom violyx_v0.2.1.iso -m 512 -nic user,model=e1000 -device intel-hda -device hda-duplex
```

**Real PC:** write `violyx_v0.2.1.img` to a USB stick (for example with Rufus in DD mode, this
erases the stick), turn off Secure Boot and start from the stick.

Once it runs, **F1** shows the help and **F2** all programs. All keys are in the
[guide](docs/guide.md).

## Build it yourself

On Linux:

You need git, gcc, make, nasm, binutils and python3

```sh
git clone https://github.com/Merecete/Violyx.git
cd Violyx
make
```

The finished images are in the `build` folder.

## Limitations

- Only tested in QEMU, VirtualBox and on one real PC
- The browser has no JavaScript and no style sheets, many modern sites will not work
- Network cards: Intel E1000 and Realtek RTL8125B/RTL8126 only, no Wi-Fi
- One monitor, no sound over HDMI
- Installing next to another system needs UEFI and a GPT disk, partitions are not shrunk

## License

MIT, see [LICENSE](LICENSE). Bundled components keep their own licenses, listed in
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

![How Violyx is built](docs/architecture.png)
