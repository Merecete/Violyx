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

## What a program can do

- It runs in its own protected memory. A crash closes only that program.
- It draws into a text grid (`vx_put`, `vx_text`, `vx_show`) and reads keys (`vx_wait`, `vx_key`).
- Relative file names live in its private data folder. It can also use `/home`, `/downloads`
  and `/drives`, but never `/system` or other programs. Violyx asks before a drive is opened.
- Esc and function keys belong to Violyx. Esc always closes the program window.

The whole API is in `violyx.h`. Installed programs are not sandboxed from your personal files in
`/home`, so only install programs you trust.
