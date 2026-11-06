# Third-Party Notices

Violyx itself is released under the MIT License (see `LICENSE`). The components below
keep their own licenses. Their notices must be included when Violyx is redistributed,
including as a prebuilt image.

| Component | Purpose | License | License text |
|---|---|---|---|
| Limine 9.x | BIOS and UEFI bootloader | BSD-2-Clause | `third_party/limine/LICENSE` |
| Limine dependencies | Included in the Limine binaries | BSD, MIT, Zlib, Apache-2.0 WITH LLVM-exception and others | Overview in `third_party/limine/3RDPARTY.md` |
| `kernel/limine.h` | Limine boot protocol | 0BSD | File header |
| lwIP 2.2.1 | TCP/IP stack | BSD-3-Clause | `third_party/lwip/COPYING` |
| Mbed TLS 3.6.7 | TLS, cryptography, certificate verification | Apache-2.0 (chosen from Apache-2.0 OR GPL-2.0-or-later) | `third_party/mbedtls/LICENSE` |
| stb_image 2.30 (Sean Barrett) | Image and gzip decoding, in the protected helper `sdk/helpers/vxdec.c` | MIT or Public Domain | `third_party/stb/LICENSE` |
| minimp3 (lieff) | MP3 decoding in XZN | CC0-1.0 | `third_party/minimp3/LICENSE` |
| stb_vorbis (Sean Barrett) | Ogg Vorbis decoding in XZN | MIT or Public Domain | `third_party/stb/LICENSE` |
| dr_flac (David Reid) | FLAC decoding in XZN | MIT-0 or Public Domain | End of `third_party/dr_flac/dr_flac.h` |
| Xenon-Zones (Merecete) | Design and intro sample of XZN (`sdk/assets/xzn-intro.mp3`) | MIT | https://github.com/Merecete/XZN |
| Mozilla root certificates (via certifi) | Trusted certificate authorities | MPL-2.0 | `third_party/cacert/LICENSE-certifi.txt` |
| OpenBSD `rge` driver | Realtek RTL8125/8126 network support (`kernel/rge.c`, `kernel/rge_reg.h`, adapted, no Realtek firmware included) | ISC | File header |
| Spleen 8x16 | System font | BSD-2-Clause | `third_party/spleen/LICENSE` |
