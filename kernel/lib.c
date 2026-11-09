#include "os.h"
/* Heap: first fit, neighbours are merged when freeing */
typedef struct blk {
    u64 size, free;
    struct blk *next;
    u64 pad;
} blk;
static blk *head;
void heap_init(u64 s, u64 e) {
    head = (blk *)s;
    head->size = e - s - sizeof(blk);
    head->free = 1;
    head->next = 0;
}
void *kmalloc(size_t n) {
    n = (n + 15) & ~15u;
    for (blk *b = head; b; b = b->next)
        if (b->free && b->size >= n) {
            if (b->size >= n + sizeof(blk) + 64) {
                blk *r = (blk *)((u8 *)(b + 1) + n);
                r->size = b->size - n - sizeof(blk);
                r->free = 1;
                r->next = b->next;
                b->next = r;
                b->size = n;
            }
            b->free = 0;
            return b + 1;
        }
    return 0;
}
void kfree(void *p) {
    if (!p)
        return;
    ((blk *)p - 1)->free = 1;
    for (blk *b = head; b && b->next;) {
        if (b->free && b->next->free) {
            b->size += b->next->size + sizeof(blk);
            b->next = b->next->next;
        } else
            b = b->next;
    }
}
u64 heap_free(void) {
    u64 t = 0;
    for (blk *b = head; b; b = b->next)
        if (b->free)
            t += b->size;
    return t;
}
void *dma_alloc(size_t n) {
    u8 *p = kmalloc(n + 8192);
    if (!p)
        panic("Kein DMA-Speicher");
    p = (u8 *)(((u64)p + 4095) & ~4095ull);
    memset(p, 0, n);
    return p;
}
