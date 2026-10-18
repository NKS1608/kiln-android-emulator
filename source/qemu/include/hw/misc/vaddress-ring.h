/*
 * Host side of the VAddressStream byte rings (kiln/docs/vaddress-guest.md
 * section 6). Self-contained so it can be unit tested without a device.
 *
 * Every header field lives in guest memory and may change under our feet,
 * so each one is read once, validated against the block size, and only the
 * validated copy is used.
 *
 * Also built into kiln-renderd (C++, outside QEMU), hence the dual licence and
 * the MIN fallback.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later OR MIT
 */

#ifndef HW_MISC_VADDRESS_RING_H
#define HW_MISC_VADDRESS_RING_H

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#define VAS_HEADER_SIZE         0x1000
#define VAS_HOST_STATE          0x008
#define VAS_NAME                0x014
#define VAS_NAME_SIZE           44
#define VAS_GUEST_WRITE_STATE   0x040
#define VAS_GUEST_READ_STATE    0x044
#define VAS_TO_HOST             0x080 /* guest -> host ring descriptor */
#define VAS_FROM_HOST           0x100 /* host -> guest ring descriptor */

/* Ring descriptor field offsets */
#define VAS_RING_DATA_OFFSET    0x00
#define VAS_RING_SIZE           0x04
#define VAS_RING_WRITE_POS      0x10
#define VAS_RING_WRAP_POS       0x14
#define VAS_RING_READ_POS       0x40

/* guest_write_state: (n << 2) | 3 = writer blocked; guest_read_state: 2 */
#define VAS_WRITER_BLOCKED(s)   (((s) & 3) == 3)
#define VAS_READER_BLOCKED      2

/* Header fields are little-endian u32. */
static inline uint32_t vas_load(const uint8_t *blk, size_t off)
{
    uint32_t v = __atomic_load_n((const uint32_t *)(blk + off),
                                 __ATOMIC_ACQUIRE);
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    v = __builtin_bswap32(v);
#endif
    return v;
}

static inline void vas_store(uint8_t *blk, size_t off, uint32_t val)
{
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    val = __builtin_bswap32(val);
#endif
    __atomic_store_n((uint32_t *)(blk + off), val, __ATOMIC_RELEASE);
}

/* Is a guest thread about to sleep (PING 0x76) waiting for the host? */
static inline bool vas_guest_waiting(const uint8_t *blk)
{
    return VAS_WRITER_BLOCKED(vas_load(blk, VAS_GUEST_WRITE_STATE)) ||
           vas_load(blk, VAS_GUEST_READ_STATE) == VAS_READER_BLOCKED;
}

/* Is the guest reader asleep (or about to be) on the from-host ring? */
static inline bool vas_reader_blocked(const uint8_t *blk)
{
    return vas_load(blk, VAS_GUEST_READ_STATE) == VAS_READER_BLOCKED;
}

typedef struct VASRing {
    uint8_t *data;
    uint32_t size;
} VASRing;

/* Validate the descriptor at @desc; false if the guest set it up wrongly. */
static inline bool vas_ring_get(uint8_t *blk, size_t blk_size, size_t desc,
                                VASRing *r)
{
    uint32_t off = vas_load(blk, desc + VAS_RING_DATA_OFFSET);
    uint32_t size = vas_load(blk, desc + VAS_RING_SIZE);

    if (blk_size < VAS_HEADER_SIZE || size < 2 || off < VAS_HEADER_SIZE ||
        off > blk_size || size > blk_size - off) {
        return false;
    }
    r->data = blk + off;
    r->size = size;
    return true;
}

/*
 * Zero-copy reading of the to-host ring: the contiguous unread bytes start at *@data (0 when the ring is empty, -1 if
 * its state is corrupt); vas_ring_consume() then frees @n of them, at most what vas_ring_peek() returned.
 */
static inline ssize_t vas_ring_peek(uint8_t *blk, size_t blk_size, uint8_t **data)
{
    size_t desc = VAS_TO_HOST;
    VASRing r;

    if (!vas_ring_get(blk, blk_size, desc, &r)) {
        return -1;
    }
    uint32_t wp = vas_load(blk, desc + VAS_RING_WRITE_POS);
    uint32_t wrap = vas_load(blk, desc + VAS_RING_WRAP_POS);
    uint32_t rp = vas_load(blk, desc + VAS_RING_READ_POS);

    if (wp >= r.size || rp >= r.size || wrap > r.size) {
        return -1;
    }
    if (wp < rp && wrap <= rp) {
        /* wp < rp: data is [rp, wrap) then [0, wp); the tail is used up */
        rp = 0;
        vas_store(blk, desc + VAS_RING_READ_POS, rp);
    }
    if (rp == wp) {
        return 0;
    }
    *data = r.data + rp;
    return (wp > rp ? wp : wrap) - rp;
}

static inline void vas_ring_consume(uint8_t *blk, size_t blk_size, size_t n)
{
    size_t desc = VAS_TO_HOST;
    uint32_t rp = vas_load(blk, desc + VAS_RING_READ_POS) + n; /* the host's own field */
    uint32_t wp = vas_load(blk, desc + VAS_RING_WRITE_POS);

    if (wp < rp && rp == vas_load(blk, desc + VAS_RING_WRAP_POS)) {
        rp = 0;
    }
    vas_store(blk, desc + VAS_RING_READ_POS, rp);
}

/*
 * Consume up to @len bytes of the to-host ring into @buf.
 * Returns bytes read, or -1 if the ring state is corrupt.
 */
static inline ssize_t vas_ring_read(uint8_t *blk, size_t blk_size,
                                    void *buf, size_t len)
{
    size_t total = 0;

    while (total < len) {
        uint8_t *data;
        ssize_t n = vas_ring_peek(blk, blk_size, &data);

        if (n < 0) {
            return total ? (ssize_t)total : -1;
        }
        if (n == 0) {
            break;
        }
        n = MIN((size_t)n, len - total);
        memcpy((uint8_t *)buf + total, data, n);
        vas_ring_consume(blk, blk_size, n);
        total += n;
    }
    return total;
}

/* Free bytes in the from-host ring, or -1 if the ring state is corrupt. */
static inline ssize_t vas_ring_room(uint8_t *blk, size_t blk_size)
{
    VASRing r;

    if (!vas_ring_get(blk, blk_size, VAS_FROM_HOST, &r)) {
        return -1;
    }
    uint32_t wp = vas_load(blk, VAS_FROM_HOST + VAS_RING_WRITE_POS);
    uint32_t rp = vas_load(blk, VAS_FROM_HOST + VAS_RING_READ_POS);

    if (wp >= r.size || rp >= r.size) {
        return -1;
    }
    return wp < rp ? rp - wp - 1 : r.size - wp + rp - 1;
}

/* Does the from-host ring hold bytes the guest has not consumed yet? */
static inline bool vas_ring_unread(uint8_t *blk, size_t blk_size)
{
    VASRing r;

    if (!vas_ring_get(blk, blk_size, VAS_FROM_HOST, &r)) {
        return false;
    }
    return vas_load(blk, VAS_FROM_HOST + VAS_RING_WRITE_POS) !=
           vas_load(blk, VAS_FROM_HOST + VAS_RING_READ_POS);
}

/*
 * Zero-copy writing of the from-host ring, mirroring the guest producer (one slot kept empty, wrap_pos marks the end
 * of valid data): the contiguous free bytes start at *@data (0 when the ring is full, -1 if its state is corrupt).
 * Fill them, then vas_ring_commit() publishes @n of them, at most what vas_ring_reserve() returned.
 */
static inline ssize_t vas_ring_reserve(uint8_t *blk, size_t blk_size, uint8_t **data)
{
    size_t desc = VAS_FROM_HOST;
    VASRing r;

    if (!vas_ring_get(blk, blk_size, desc, &r)) {
        return -1;
    }
    uint32_t wp = vas_load(blk, desc + VAS_RING_WRITE_POS);
    uint32_t rp = vas_load(blk, desc + VAS_RING_READ_POS);

    if (wp >= r.size || rp >= r.size) {
        return -1;
    }
    *data = r.data + wp;
    return wp < rp ? rp - wp - 1 : r.size - wp - (rp == 0);
}

static inline void vas_ring_commit(uint8_t *blk, size_t blk_size, size_t n)
{
    size_t desc = VAS_FROM_HOST;
    VASRing r;

    if (!vas_ring_get(blk, blk_size, desc, &r)) {
        return; /* the guest broke the header since the reserve: the next one reports it */
    }
    uint32_t wp = vas_load(blk, desc + VAS_RING_WRITE_POS); /* the host's own field */

    /* The reader cannot pass the writer, so this test cannot turn false since the reserve; if the reader wrapped to 0
     * meanwhile, wrap_pos is set as a fresh write would set it. */
    if (wp >= vas_load(blk, desc + VAS_RING_READ_POS)) {
        vas_store(blk, desc + VAS_RING_WRAP_POS, wp + n);
    }
    wp += n;
    if (wp == r.size) {
        wp = 0;
    }
    /* publish each step: the guest may read the tail before the head */
    vas_store(blk, desc + VAS_RING_WRITE_POS, wp);
}

/*
 * Produce up to @len bytes into the from-host ring.
 * Returns bytes written, or -1 if the ring state is corrupt.
 */
static inline ssize_t vas_ring_write(uint8_t *blk, size_t blk_size,
                                     const void *buf, size_t len)
{
    size_t total = 0;

    while (total < len) {
        uint8_t *data;
        ssize_t n = vas_ring_reserve(blk, blk_size, &data);

        if (n < 0) {
            return total ? (ssize_t)total : -1;
        }
        if (n == 0) {
            break;
        }
        n = MIN((size_t)n, len - total);
        memcpy(data, (const uint8_t *)buf + total, n);
        vas_ring_commit(blk, blk_size, n);
        total += n;
    }
    return total;
}

#endif
