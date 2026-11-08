# Violyx SDK

Write your own Violyx programs in C. A program is a single `.vpk` package that you can copy to a
FAT32 stick, share with others and install in Violyx: open it in Files and confirm.

You need Linux or WSL with `gcc`, `binutils`, `make` and `python3`.

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

## Sound

`vx_tone(hz, ms, volume, wave, delay)` plays a tone without blocking. The volume runs from 1 to 100, the wave is `VX_SQUARE`, `VX_TRIANGLE`, `VX_SAW` or `VX_NOISE`, and the delay in milliseconds lets you queue short melodies. Without a sound card the call does nothing.

`chip.h` adds a small chiptune player: write up to three parts as text, call `chip_play()` once and `chip_tick()` in your game loop. Novastrike and Bricks show how it works.

## Program menu

`vx_menu("Info|Music on/off")` adds a menu after **Help** in the menu bar, named after your window title. Choosing item n sends the key `VK_MENU + n` to your program. An item that starts with `*`, such as `*Info`, is handled by Violyx: `*Info` shows an Info window with the window title, the system version and the copyright. Call it again to change the items.
