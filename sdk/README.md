# Violyx SDK

Write your own Violyx programs in C. A program is a single `.vpk` package that you can copy to a
FAT32 stick, share with others and install in Violyx: open it in Files and confirm.

On Violyx itself the built-in C compiler does it: `cc snake.c -s 60x19` in Commands (see the
guide, "Writing C programs in Violyx"). On a PC you need Linux or WSL with `gcc`, `binutils`,
`make` and `python3`:

```sh
make APP=examples/snake NAME=snake TITLE=Snake VPKFLAGS='--cols 60 --rows 19 --icon " ~~~~ |  ~~@ |      "'
```

The package ends up in `build/snake.vpk`. Copy it to a stick, open `/drives/usb1` in Files and
press Enter on it. The program then appears in the Programs overview (F2). Delete it there with Del.

`make apps` builds every example. The source of all examples is in `examples/` under the MIT
License, so you can read, change and share it:

| App | Source |
|---|---|
| Hello | `examples/hello.c` |
| Snake | `examples/snake.c` |
| Bricks | `examples/bricks.c` |
| Novastrike | `examples/novastrike.c` |
| Fetch | `examples/fetch.c` (network) |
| XZN | `examples/xzn.c` (music player) |

The window can change size at any time (F5 zooms it). `vx_cols` and `vx_rows` always return the
current size, so read them once per frame as Bricks and Novastrike do.

`vx_paint` and `vx_rgb` give programs all 256 colours, not only the Violyx palette.

`png2vwp.py` converts a picture on your PC into a Violyx background.

## What a program can do

- It runs in its own protected memory. A crash closes only that program.
- It draws into a text grid (`vx_put`, `vx_text`, `vx_show`) and reads keys (`vx_wait`, `vx_key`).
- Relative file names live in its private data folder. It can also use `/home`, `/downloads`
  and `/drives`, but never `/system` or other programs. Violyx asks before a drive is opened.
- Esc and function keys belong to Violyx. Esc always closes the program window.
- Large files: `vx_read_at(name, buffer, offset, size)` reads up to 512 KB from any offset.
  Reading a big file in such parts keeps the rest of the system running smoothly, as XZN does
  when it loads a track.

The whole API is in `violyx.h`. Installed programs are not sandboxed from your personal files in
`/home`, so only install programs you trust.

The programs that come with Violyx run the same way: Write, Files, Commands, the browser, the
calculator, calendar, clock, image viewer, Paint and the games are all ring 3 programs inside the
kernel image (their source is in `system/` next to the kernel). They use the same system calls,
plus a few system services that only they get.

## Dialogs

The program can use the dialogs of Violyx. They wait for the user, the program stops meanwhile.

- `vx_message(title, text)` shows a message.
- `vx_ask(title, text)` asks a yes/no question, 1 means yes.
- `vx_input(title, prompt, buf, size)` asks for a line of text. `buf` holds the suggestion.
- `vx_choose_file(title, path, size)` lets the user pick a file anywhere, also on a stick. The
  program may then read and write exactly this file, even outside its usual folders.
- `vx_open("write", path)` opens a file in a system program: `write` for text, `view` for
  pictures, `web` for an address.

## Network

The kernel offers name lookups and TCP connections. Everything above is up to the program: plain
HTTP is a few lines (see `examples/fetch.c`), for HTTPS a program brings a TLS library such as
Mbed TLS, just like the Violyx browser does.

- The first time a program goes online, Violyx asks the user once whether it may.
- `vx_resolve(name, &ip)` and `vx_connect(ip, port)` wait until they are done (at most 6 and 15
  seconds) while the rest of the system keeps running.
- `vx_send` returns how much it took (0: wait a little and try again), `vx_recv` returns the
  bytes, `VX_AGAIN` when nothing has arrived yet and 0 when the other side has closed.
- `vx_disconnect()` frees the network for others. There is one connection at a time; while
  another program uses it, the calls return `VX_BUSY`.

## Pictures

`vx_image` uploads a picture, `vx_pixcell` shows its 4x8 pixel blocks in text cells. With
`vx_image_slot(slot, ...)` and `vx_pixcell_slot` a program can keep up to 31 more pictures at once
(together up to 24 MB).

## Sound

`vx_tone(hz, ms, volume, wave, delay)` plays a tone without blocking. The volume runs from 1 to 100, the wave is `VX_SQUARE`, `VX_TRIANGLE`, `VX_SAW` or `VX_NOISE`, and the delay in milliseconds lets you queue short melodies. Without a sound card the call does nothing.

`chip.h` adds a small chiptune player: write up to three parts as text, call `chip_play()` once and `chip_tick()` in your game loop. Novastrike and Bricks show how it works.

## Program menu

`vx_menu("Info|Music on/off")` adds a menu after **Help** in the menu bar, named after your window title. Choosing item n sends the key `VK_MENU + n` to your program. An item that starts with `*`, such as `*Info`, is handled by Violyx: `*Info` shows an Info window with the window title, the system version and the copyright. Call it again to change the items.
