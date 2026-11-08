#pragma once
#include "viofs.h"
/* Formats a whole removable drive: a new MBR with one partition at 1 MiB, then FAT32 or exFAT.
 * d covers the whole drive (start 0). Testable on the host. The boot sector is written last, so an
 * interrupted run leaves no half-valid file system behind. */
int fmt_fat32(const vfs_dev *d, const char *label, uint32_t serial,
              void (*prog)(uint64_t done, uint64_t total));
int fmt_exfat(const vfs_dev *d, const char *label, uint32_t serial,
              void (*prog)(uint64_t done, uint64_t total));
