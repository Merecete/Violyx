#pragma once
#include "viofs.h"
/* Audio CDs: every track appears as a WAV file ("01 Title.wav" or "Track 01.wav"), made on the fly
 * from the raw audio of the disc (2352 bytes per sector: 44.1 kHz, 16 bit, stereo). Titles come
 * from CD-Text when the disc has it. "cdtoc.txt" holds the table of contents in the form the
 * MusicBrainz lookup expects. Self-contained and testable on the host; everything read from the
 * disc counts as hostile. */
#define CDA_MAX  99
#define CDA_TEXT 64
typedef struct cdda {
    int n;                        /* audio tracks */
    int first, last;              /* track numbers on the disc */
    uint32_t lead;                /* start of the lead-out */
    uint8_t num[CDA_MAX];         /* track number of each audio track */
    uint32_t start[CDA_MAX];      /* first sector */
    uint32_t len[CDA_MAX];        /* sectors */
    uint32_t tstart[CDA_MAX + 1]; /* by track number, also data tracks: for the TOC */
    uint8_t isdata[CDA_MAX + 1];
    /* CD-Text by track number on the disc, [0] is the album */
    char title[CDA_MAX + 1][CDA_TEXT], artist[CDA_MAX + 1][CDA_TEXT];
    char name[CDA_MAX][CDA_TEXT + 12];
    char toc[CDA_MAX * 8 + 40]; /* cdtoc.txt */
    /* n sectors of 2352 bytes from sector lba on; 0 = ok */
    int (*rd)(void *ctx, uint32_t lba, uint32_t n, void *buf);
    void *ctx;
} cdda;
/* toc: answer of READ TOC (format 0, LBA addresses). 0 = at least one audio track */
int cda_toc(cdda *a, const uint8_t *toc, int len);
/* text: answer of READ TOC format 5 (CD-Text), may be called with nothing */
void cda_text(cdda *a, const uint8_t *text, int len);
void cda_names(cdda *a);
int cda_stat(cdda *a, const char *p, vfs_ent *e);
int64_t cda_read_at(cdda *a, const char *p, void *b, uint64_t off, uint64_t max);
int cda_list(cdda *a, const char *p, int (*cb)(const vfs_ent *, void *), void *arg);
int cda_header(cdda *a, int i, uint8_t *h); /* the WAV header of track i, returns its length */
