# Violyx 0.1.0

Eigenes 64-Bit-Betriebssystem für x86-PCs mit tastaturgesteuertem Desktop in
Textoptik. Es startet per BIOS und UEFI über Limine. Kernel, Treiber, Dateisystem
und Oberfläche sind eigener Code.

## Starten in QEMU

    qemu-system-x86_64 -drive file=violyx.img,format=raw -m 256

Mit UEFI (Pfad zur Firmware je nach Installation):

    qemu-system-x86_64 -drive if=pflash,format=raw,readonly=on,file=edk2-x86_64-code.fd -drive file=violyx.img,format=raw -m 256

## VirtualBox

Neue VM mit Typ "Other", Version "Other/Unknown (64-bit)", mindestens 256 MB RAM.

- `violyx.vdi` als vorhandene Festplatte einbinden: Dateien werden gespeichert.
- `violyx.iso` als CD einbinden: nur zum Ausprobieren, ohne Speichern.

Für UEFI in der VM "EFI aktivieren" einschalten.

Für Netzwerk den Adapter auf "Intel PRO/1000 MT Desktop" stellen. Im Befehlsfenster zeigt
`netz` den Status, `ping NAME` testet die Verbindung, `hole ADRESSE` ruft eine Seite ab.

## Sprache und Tastatur

Unter System, Einstellungen lassen sich Sprache (Deutsch, English) und Tastaturlayout
(Deutsch, US) getrennt umstellen. Das wirkt sofort, bleibt gespeichert und wird bei der
Installation übernommen.

## Installieren

Von `violyx.iso` oder dem USB-Stick starten, dann System, Installieren. Violyx belegt die
gesamte gewählte Platte, alle Daten darauf gehen verloren. Die Vorauswahl ist immer Nein,
danach muss LÖSCHEN (auf Englisch DELETE) eingetippt werden. Das Installieren in eine einzelne Partition neben
anderen Systemen wird noch nicht unterstützt.

## USB-Stick

`violyx.img` lässt sich mit Rufus im DD-Modus auf einen Stick schreiben. Dabei wird
der Stick komplett überschrieben. Violyx kann auf dem Stick auch speichern.

## Selbst bauen (Linux)

Benötigt: gcc, nasm, make, binutils, python3

    make
    make run

Die Ergebnisse liegen in `build/`: `violyx.img`, `violyx.iso` und `violyx.vdi`.

## Bedienung

F1 Hilfe, F2 Programmübersicht, F3 Dateien, F4 Befehle, F5 Zoom, F6 Fenster wechseln,
F7 Internet, F9 Rechner, F10 Menü. Alt+S/P/F/H öffnet Menüs. Esc schließt das aktive Fenster.
Strg+Pfeile verschiebt, Umschalt+Pfeile ändert die Größe.

## Aufbau

- `kernel/entry.asm`, `main.c`: Einstieg, Interrupts, Speicher, Timer, Uhr
- `kernel/ui.c`: Grafikausgabe, Fenster, Menüs, Dialoge
- `kernel/kbd.c`: Tastatur mit deutschem Layout, gemeinsam für PS/2 und USB
- `kernel/pci.c`: Geräteerkennung
- `kernel/disk.c`: Blockschicht, IDE und VioFS
- `kernel/ahci.c`, `nvme.c`: SATA und NVMe
- `kernel/xhci.c`: USB 3 mit Tastaturen und USB-Speicher
- `kernel/acpi.c`: Ausschalten über ACPI
- `kernel/net.c`: Intel-E1000-Treiber, lwIP-Anbindung, DNS, Ping und TCP
- `kernel/tls.c`, `http.c`: HTTPS über Mbed TLS, HTTP-Abruf
- `kernel/install.c`: Installer
- `kernel/html.c`, `browser.c`: HTML-Layout (auch auf dem Host testbar) und Textbrowser
- `kernel/apps.c`: Programme

Neue Treiber melden sich bei der Blockschicht mit `blk_register()` an.
USB-Eingaben laufen über `kbd_in()` durch denselben Tastaturtreiber wie PS/2.

## Bekannte Grenzen

- Nur in QEMU getestet, noch nicht in VirtualBox oder auf echter Hardware.
- Keine USB-Hubs, kein Einstecken im laufenden Betrieb, nur xHCI-Controller.
- USB-Speicher und NVMe nur mit 512-Byte-Sektoren. Von der ISO aus kein Speichern.
- Secure Boot muss aus sein, weil Violyx nicht signiert ist.
- Netzwerk nur über Intel E1000, kein WLAN.
- Browser ohne JavaScript, Bilder und Cookies, Seiten bis 4 MB. Formulare mit Text und Knöpfen, ohne Auswahllisten und Passwortfelder.
- HTTPS prüft Zertifikate, aber keine Sperrlisten. Die Stammzertifikate werden nur mit Updates erneuert.
- Antworten mit `hole` sind auf 1 MB begrenzt.
- Keine getrennten Prozesse, kein Speicherschutz. Daher keine Passwörter verwenden.

Datenträger werden nur beschrieben, wenn sie eine Violyx-GPT-Partition mit
gültiger VioFS-Signatur haben. Formatiert wird nie automatisch.

## Lizenz

MIT, siehe `LICENSE`. Fremdkomponenten: siehe `THIRD-PARTY-NOTICES.md`.
