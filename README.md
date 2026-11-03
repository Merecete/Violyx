# Violyx

**A keyboard-only 64-bit operating system for x86 PCs, with a retro text desktop in glowing violet.**

Violyx is written from scratch: its own kernel, drivers, file system, windowed text
desktop and programs. There is no Linux underneath. It boots on BIOS and UEFI, connects
to the internet and browses the web in a text browser, all without ever touching a mouse.

![Violyx boot logo](docs/logo.png)

| | |
|---|---|
| ![Programs](docs/programs.png) | ![Text browser](docs/browser.png) |

> **Status: experimental hobby project (version 0.1.0).** Tested in QEMU and VirtualBox,
> not yet on real hardware. Violyx has no memory protection, so please do not enter
> passwords or use it for anything sensitive. See [Limitations](#limitations).

## Features

- **Keyboard-only desktop** with overlapping windows, menus and dialogs
- **Text browser** with HTTPS, certificate checks, links, search forms and link numbers
- **Programs:** Write, Files, Commands, Calculator, Pong, Settings
- **Networking:** Intel E1000 driver, DHCP, DNS, ping, TCP/IP via lwIP
- **HTTPS:** TLS 1.2 and 1.3 via Mbed TLS with Mozilla root certificates
- **Storage:** IDE, AHCI (SATA), NVMe and USB mass storage, with its own file system
- **USB keyboards** on xHCI controllers, plus PS/2
- **Installer** with several safety prompts
- **Two languages:** English and German, with US and German keyboard layouts
- **Boots on BIOS and UEFI** through the Limine bootloader

## Try it

Download `violyx.iso` or another image from the [Releases](../../releases) page.

### QEMU

```sh
qemu-system-x86_64 -drive file=violyx.img,format=raw -m 512 -nic user,model=e1000
```

For UEFI, also pass your QEMU firmware file:

```sh
qemu-system-x86_64 -drive if=pflash,format=raw,readonly=on,file=edk2-x86_64-code.fd \
  -drive file=violyx.img,format=raw -m 512 -nic user,model=e1000
```

### VirtualBox

1. Create a VM with type **Other** and version **Other/Unknown (64-bit)**. The 64-bit
   version matters: Violyx does not run on the 32-bit setting.
2. Give it at least **512 MB** of memory.
3. Attach `violyx.vdi` as an existing hard disk, or use `violyx.iso` to try Violyx
   without saving.
4. Set the network adapter type to **Intel PRO/1000 MT Desktop**.
5. Optional: enable EFI to boot through UEFI.

### USB stick

Write `violyx.img` to a stick, for example with Rufus in DD mode. This erases the
stick. Secure Boot must be turned off because Violyx is not signed.

## Keyboard

| Key | Action |
|---|---|
| F1 | Help |
| F2 | Programs overview |
| F3 / F4 | Files / Commands |
| F5 / F6 | Zoom / switch window |
| F7 | Internet |
| F9 | Calculator |
| F10 | Menu |
| Alt+S, P, W, H | Open a menu directly |
| Esc | Close the window |
| Ctrl+Arrows / Shift+Arrows | Move / resize the window |

In the browser: Tab selects links, Enter opens them, Backspace goes back, Ctrl+L opens
the address bar. Press N to show link numbers, then type a number and Enter.

## Language and keyboard layout

Violyx starts in English with a US keyboard layout. Change both under
**System → Settings**. The choice takes effect immediately, is saved, and carries over
into an installation.

## Installing

Start Violyx from the ISO or a USB stick and open **System → Install**.

**Violyx always uses the entire disk and erases everything on it.** The default answer
is always No, and you must type `DELETE` to continue. Installing into a single partition
next to another system is not supported yet. Please check the disk model carefully.

## Build from source

You need Linux with `gcc`, `nasm`, `make`, `binutils` and `python3`.

```sh
make
```

The results are in `build/`: `violyx.img`, `violyx.iso` and `violyx.vdi`.

## Project layout

| Path | Contents |
|---|---|
| `kernel/main.c`, `entry.asm` | Startup, interrupts, memory, timers, clock |
| `kernel/ui.c` | Graphics output, windows, menus, dialogs |
| `kernel/apps.c`, `browser.c` | Programs and text browser |
| `kernel/html.c` | HTML layout, also testable on a host system |
| `kernel/kbd.c`, `xhci.c` | Keyboard layouts, USB |
| `kernel/disk.c`, `ahci.c`, `nvme.c` | Block layer, IDE, file system, storage drivers |
| `kernel/net.c`, `tls.c`, `http.c` | Network driver, TLS, HTTP |
| `kernel/install.c`, `acpi.c` | Installer, power off |
| `tools/` | Image builders and test tools |
| `third_party/` | Bundled components |

## Limitations

- No memory protection or separate processes: a bug can bring down the whole system.
- Browser: no JavaScript, images, cookies or password fields. Many modern sites will not work.
- Network: Intel E1000 cards only, no Wi-Fi.
- USB: xHCI only, no hubs, devices must be connected at boot.
- File system: 62 files of up to 64 KB each, no folders, 8 MB data area.
- NVMe and USB storage: 512-byte sectors only.
- Root certificates only update with new Violyx releases. Certificate revocation is not checked.

## License

Violyx is released under the [MIT License](LICENSE). Bundled components keep their own
licenses, listed in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).
