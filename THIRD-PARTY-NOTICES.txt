Violyx - Third-party software
==================================================================

Violyx itself is released under the BSD 3-Clause License (see
LICENSE), XZN for Violyx under the MIT License (see
sdk/examples/LICENSE-XZN).

Violyx contains the following open-source components. Each one
stays under its own license. The full license texts are in the
source code at the paths given below, and Violyx shows all of
them under System > About Violyx > L. Their notices must be
included when Violyx is redistributed, also as a prebuilt image.

------------------------------------------------------------------
Component                License
  What it does, where the license text is
------------------------------------------------------------------
Limine 9.x               BSD-2-Clause
  BIOS and UEFI bootloader, third_party/limine/LICENSE
Limine dependencies      BSD, MIT, Zlib, Apache-2.0 WITH
                         LLVM-exception and others
  inside the Limine binaries, third_party/limine/3RDPARTY.md
kernel/limine.h          0BSD
  Limine boot protocol, in the file header
lwIP 2.2.1               BSD-3-Clause
  TCP/IP stack, third_party/lwip/COPYING
Mbed TLS 3.6.7           Apache-2.0 (chosen from
                         Apache-2.0 OR GPL-2.0-or-later)
  TLS and certificates, third_party/mbedtls/LICENSE
stb_image 2.30           MIT or Public Domain
  images and gzip in sdk/helpers/vxdec.c,
  third_party/stb/LICENSE
stb_vorbis               MIT or Public Domain
  Ogg Vorbis in XZN, third_party/stb/LICENSE
minimp3                  CC0-1.0
  MP3 in XZN, third_party/minimp3/LICENSE
dr_flac                  MIT-0 or Public Domain
  FLAC in XZN, end of third_party/dr_flac/dr_flac.h
Xenon-Zones              MIT
  design and intro sample of XZN (sdk/assets/xzn-intro.mp3),
  https://github.com/Merecete/XZN
Mozilla root             MPL-2.0
certificates (certifi)
  trusted certificate authorities,
  third_party/cacert/LICENSE-certifi.txt
OpenBSD rge driver       ISC
  Realtek RTL8125/8126 (kernel/rge.c, kernel/rge_reg.h,
  adapted, no Realtek firmware), third_party/rge/LICENSE
Spleen 8x16              BSD-2-Clause
  system font, third_party/spleen/LICENSE
chibicc                  MIT
  the C compiler cc (system/cc/chibicc, adapted, and
  system/cc/include-cc), system/cc/chibicc/LICENSE
ATSC A/52 tables         data from a public standard
  two number tables of the AC3 decoder in XZN (latab, hth in
  sdk/examples/xzncodec.h), from ATSC A/52:2018
------------------------------------------------------------------

The source code of Violyx and of every component listed here is
in the Violyx source code: https://github.com/Merecete/Violyx

Patent note: some formats (for example AC3) may be covered by
patents in some countries. Violyx only uses them to play files.
