# Violyx guide

All keys and details. The short version is in the [README](../README.md), and F1 in Violyx shows help too.

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

New windows open in the centre. The **Windows** menu (Alt+W) can zoom the window or move
it to the centre or any corner.

Most programs add their own menu after **Help**, named after the program, with an Info
window showing the name, the version and the copyright. Files offers copy, move, delete,
new file or folder, eject and format there, XZN its Info window, keys, colour scheme and
mini player, and the games music on/off and pause.

In Files: Enter opens, Backspace goes up, N creates a file, O creates a folder, C
copies, R renames or moves, Del deletes. In `/drives`, E ejects a drive and F formats it.

In Calendar: arrows pick a day, Page Up/Down change the month, T jumps to today, Enter
adds a note and Del removes it. Notes are saved in `/system/calendar`.

Clock is an analog clock with a ticking second hand, with the date and the digital time
below. Make the window bigger and the clock grows with it.

Image viewer opens PNG, JPEG, GIF, BMP and TGA files when you press Enter on them in Files
(or with `view FILE` in Commands). Pictures look the same as in the browser: large ones are
scaled down to the window, small ones keep their size. Left and Right show the previous and
next picture in the folder, F fits the picture to the window, Enter makes the window as big
as the screen. H (or B) uses the picture as background.

The Programs menu lists the overview and all open windows, so you can jump to any of them.

In Commands: `dir`, `cd`, `pwd`, `mkdir`, `del`, `mv`, `cp`, `type`, `edit`, `view`, `clock`,
`df`, `eject`, `format`, `usb`, `sound`, `devices`, `netdiag`, `install`, `run`, `uninstall`, `cc` and
more. `help` lists them by topic and `help cp` explains one. Up and Down recall earlier inputs, Left/Right, Home/End
and Del edit the line, and Tab completes commands, folders and file names. Names with spaces go
in quotes: `cp "My song.mp3" /drives/usb1`.

## Browser

| Key | Action |
|---|---|
| Tab / Shift+Tab | Next / previous link, wraps around at the end of the page |
| Enter | Open the link |
| Backspace | Back one page |
| Ctrl+L | Address bar |
| N | Show link numbers, then type a number and Enter |
| Ctrl+F / Ctrl+G | Search on the page / next hit |
| Ctrl+D / Ctrl+B / Ctrl+H | Add bookmark / bookmarks / history |
| Ctrl+T / Ctrl+Enter | New tab / open the link in a new tab |
| Ctrl+Page Up/Down | Switch tabs |
| Ctrl+I | Images off or on for this site |
| Ctrl+R / Ctrl+S | Reload / save the page to `/downloads` |
| Ctrl+P | Save the image on the screen to `/downloads` |
| Esc | End the search, then close the tab, then the window |

Images load after the text and are shown in 256 colours. Files the browser cannot show are
offered as a download, and a downloaded `.vpk` can be installed right away, so a simple web
page with links to `.vpk` files works as a program store. Ctrl+P saves the image on the screen
(or the first one of the page) unchanged, as the original PNG or JPEG. The whole browser runs
as a protected program, images and compressed pages included, so a broken page or image
cannot harm the system. The history page also clears history and cookies.

## Sound

Violyx plays sound through Intel HD Audio, AC'97 and USB audio devices such as headsets,
USB sound adapters and mainboard chips like the Realtek ALC4080, which is connected
over USB on many current mainboards. USB devices with separate front and rear outputs
play on both.

Volume and output (Automatic, Speakers or HDMI) are in **System → Settings**. The
`sound` command lists all sound chips with their state and plays a short test.

## XZN

XZN is the Violyx edition of the [Xenon-Zones](https://github.com/Merecete/XZN) music player.
It plays MP3, OGG, FLAC and WAV files up to 200 MB from your disk or a USB stick, with a
10-band equalizer, spectrum, playlist, mini player and five colour schemes. Music keeps
playing behind other windows, even while you play a game.

| Key | Action |
|---|---|
| Space | Play / pause |
| Left / Right | Seek 5 seconds |
| Page Up / Down | Seek 1 minute |
| Up / Down | Volume |
| N / P | Next / previous track |
| S / M | Stop / mute |
| R / Z | Repeat / shuffle |
| B / V | Balance left / right |
| E / C / W / I | Equalizer / colour scheme / mini player / file info |
| A | Add files, F adds a whole folder |
| Tab | Use the playlist: Enter plays, Del removes, X clears the list, / searches |
| F1 or H | All keys |

The XZN menu in the menu bar opens the Info window, the key list, the next colour scheme and
the mini player. Tracks load in parts, so other programs keep running while a large file
loads. The mini player looks like the original: the title runs as a ticker, the volume slider sits next
to the time (Up/Down), and the buttons and the seek bar are at the bottom.

## Screen savers

Under **System → Settings → Screen saver** choose one of five screen savers and under **Wait**
how long Violyx waits without a key press (1 to 30 minutes). Enter on the screen saver row shows
a preview. Any key ends the screen saver; the key itself is not passed on. Music in XZN keeps
playing.

- **Violyx logo:** the big logo, the stars around it glow up and fade, each in its own rhythm.
- **Wire frames:** cube, octahedron, icosahedron, tetrahedron, pyramid and a cube in a cube,
  turning in 3D, a new one every 12 seconds.
- **Digital clock:** seven-segment digits with blinking colon, seconds and date. It moves a
  little every minute so nothing burns in.
- **Analog clock:** the clock with its second hand across the whole screen.
- **Synthwave:** the sun rises over the mountains, the road with palms and the grid floor run
  towards you.

## Colour schemes, Paint and backgrounds

Don't like violet? Under **System → Settings → Colour scheme** you can switch the whole
desktop to Ocean, Emerald, Amber, Ruby or Graphite. The change is immediate and saved.
Games, installed programs such as XZN, Paint, the boot logo and picture backgrounds keep
their own violet colours.

Choose a background under **System → Settings**: patterns, the Violyx logo or your own
image. Paint draws backgrounds in 256 colours, and they are stretched to any screen size.
`sdk/png2vwp.py` converts your own pictures.

Any picture can become the background in the Image viewer with **H** (or **B**, or in its
menu). It fills the whole screen. Violyx remembers where the picture came from: while the
file is missing, for example because its USB stick is not plugged in, the background from
the settings is shown, and the picture comes back as soon as the stick is there again. To
go back to a background from the settings, press Left or Right on Background in Settings.

## USB drives

Plug in a FAT32 or exFAT stick and it appears in the `/drives` folder as `usb1`, `usb2` and so on.
Violyx reads nothing from a stick until you allow it: the first time you open a drive it
asks for confirmation. Answer `Y` or `J` to read and save files there. Only USB drives are
listed, never internal disks. Use **E** in Files or `eject` in Commands before unplugging.

SD and other memory cards work in a USB card reader. Every slot of the reader is its own
drive, and a card appears about a second after you put it in, even if the reader was
plugged in before. Card readers built into the PC that are not connected over USB are not
supported.

Sticks can be plugged in while Violyx is running, and the live ISO can save to them too.
Files use long names and are compatible with Windows, macOS and Linux. exFAT also holds
files over 4 GB, as most sticks of 64 GB and more come formatted. NTFS is not supported.

**Formatting:** select a drive in `/drives` and press **F** (or `format usb1` in Commands).
Choose FAT32 (works everywhere, also in car radios and TVs) or exFAT (for files over 4 GB),
give the drive a name, confirm the warning and type `FORMAT` (`FORMATIEREN` in German).
Violyx writes a new partition table with one partition at 1 MiB. Only USB drives can be
formatted, never the drive Violyx is running from. FAT32 works on sticks of any size up to
2 TB, also above the 32 GB that Windows allows.

## Games

Open them from the Programs overview (F2) or the Programs menu. Esc closes a game. All
games resize with their window and have sound.

| Game | Goal | Keys |
|---|---|---|
| Pong | Beat the computer, or play against a friend (key 2) | Alone: Up and Down or W and S; two players: left W and S, right Up and Down |
| Invaders | Stop the alien waves | Arrows move, Space fires, P pauses |
| Chomp | Eat all dots and avoid the ghosts | Arrows or WASD move, P pauses, M music |
| Mines | Uncover every safe field | Arrows move, Space opens, F flags, 1 to 3 size, N new game |
| Snake | Eat and grow without crashing | Arrows move, Enter restarts |
| Bricks | Clear the wall of bricks | Arrows or A and D move, Space launches, P pauses, M music |
| Novastrike | Free all ten worlds | Arrows or WASD fly, Space fires, X shield, C nova, P pauses, M music |

Chomp, Bricks and Novastrike play chiptune music. M or the game's own menu in the menu bar
turns it off and on again, and Bricks and Novastrike remember the choice.

In Bricks, hitting the ball with the edge of the paddle makes it faster, and a fast ball
smashes through several bricks. Its 16 levels bring tough bricks that need two hits and solid
ones that never break. After the last level the walls come back faster. Novastrike has ten worlds with their own colours, music
and boss, and you win once all ten are free.

All games run as protected programs. Snake, Bricks, Novastrike and XZN are SDK programs with
their source in `sdk/examples/`, the other games are in `system/`.

## Installing programs

Programs are single `.vpk` files that you can share, for example on a FAT32 stick. Open one in
Files and confirm to install it. It then appears in the Programs overview (F2). Select it
there and press Del to uninstall it together with its data. Commands offers `install`, `run`
and `uninstall`. `run FILE.vpk` starts a package directly without installing it, and the live
ISO offers the same when you open one in Files. The built-in programs are part of Violyx and
cannot be uninstalled.

Each program runs in its own protected memory, so a crash closes only that program. A program
can use its private data folder, `/home`, `/downloads` and `/drives`, but not `/system` or other
programs. It can still read and change your files in `/home`, so only install programs you trust.
A file the user picks in a program's file dialog may be used too, wherever it is. The first time
a program wants to go online, Violyx asks whether it may.

To write your own program in C, see [sdk/README.md](sdk/README.md). Programs can play sounds
and music, show pictures, use the Violyx dialogs and the network.

## Writing C programs in Violyx

Violyx has its own C compiler, so you can write programs on Violyx itself. Write the program
in Write, save it as a `.c` file and compile it in Commands:

    cc spiel.c

This makes `spiel.vpk` next to the source and offers to start it with Enter. Errors are shown
with the line and a marker under the place. Options: `-o NAME.vpk` for another output file,
`-s 60x18` for the window size in columns and rows (20-76 x 5-20, default 76x20), `-t Title`
for the window title, `-D NAME` defines a macro, `-I folder` adds a folder for `#include` and
`-S` also keeps the assembly as `.s`.

The compiler understands C11 (without threads) and brings
its own headers: `stdio.h`, `stdlib.h`, `string.h`, `math.h`, `ctype.h`, `time.h`, `stdint.h`,
`stdbool.h`, `limits.h`, `assert.h` and the others of the standard, plus `violyx.h` and
`chip.h` from the SDK. A program with `main(int argc, char **argv)` writes with `printf` into
its window and reads lines with `scanf` or `fgets`; PgUp/PgDn scroll back, and after the end
Esc closes the window. A program with the SDK's `vx_` functions draws itself, like the games in
`sdk/examples`, which compile unchanged (`cc snake.c -s 60x19`). Files are opened relative to
the folder of the source. The finished `.vpk` is an ordinary package: `run`, `install` or share
it like any other.

The compiler is built from chibicc by Rui Ueyama (MIT license) with an own assembler that
writes the program directly. It runs like a normal program: it can read and write the folder of
the source, `/home`, `/downloads` and `/drives`, but not `/system`.

## Language and keyboard layout

Violyx starts in English with a US keyboard layout. Change both under
**System → Settings**. The choice takes effect immediately, is saved, and carries over
into an installation.

## Installing

Start Violyx from the ISO or a USB stick and open **System → Install**.

There are two ways. The default answer is always No, and before anything is erased you
must type `DELETE`. Please check the disk model carefully.

- **Entire disk:** Violyx erases everything on the disk and uses all of it.
- **Next to another system** (only when Violyx was started by UEFI and the disk has a GPT
  partition table): choose one partition or a free, unallocated area of at least 128 MB.
  Only that area is used. Violyx creates a small boot partition of its own ("Violyx Boot")
  and its data partition there, and adds the start entry "Violyx" to the firmware. The
  other system and its EFI partition stay untouched and still start by default. To start
  Violyx, choose "Violyx" in the boot menu of the firmware (often F11, F12 or F8).
  Violyx does not shrink partitions: make room first, for example in Windows Disk
  Management. If BitLocker is on, keep the recovery key at hand.
