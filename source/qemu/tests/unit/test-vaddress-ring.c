/*
 * Unit test for the VAddressStream host ring code.
 * The guest side is modelled from kiln/docs/vaddress-guest.md section 6.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/misc/vaddress-ring.h"

#define FROM_HOST_SIZE 64
#define TO_HOST_SIZE   64
#define BLK_SIZE (VAS_HEADER_SIZE + FROM_HOST_SIZE + TO_HOST_SIZE)

static uint8_t blk[BLK_SIZE];

static void setup(void)
{
    memset(blk, 0, sizeof(blk));
    vas_store(blk, VAS_TO_HOST + VAS_RING_DATA_OFFSET,
              VAS_HEADER_SIZE + FROM_HOST_SIZE);
    vas_store(blk, VAS_TO_HOST + VAS_RING_SIZE, TO_HOST_SIZE);
    vas_store(blk, VAS_FROM_HOST + VAS_RING_DATA_OFFSET, VAS_HEADER_SIZE);
    vas_store(blk, VAS_FROM_HOST + VAS_RING_SIZE, FROM_HOST_SIZE);
}

/* Guest producer on the to-host ring (split writes, as writeFullyImpl). */
static size_t guest_write(const uint8_t *buf, size_t len)
{
    size_t d = VAS_TO_HOST, total = 0;
    uint8_t *data = blk + VAS_HEADER_SIZE + FROM_HOST_SIZE;
    uint32_t wp = vas_load(blk, d + VAS_RING_WRITE_POS);
    uint32_t rp = vas_load(blk, d + VAS_RING_READ_POS);

    while (total < len) {
        uint32_t room = wp < rp ? rp - wp - 1 : TO_HOST_SIZE - wp - (rp == 0);
        size_t n = MIN(room, len - total);
        if (!n) {
            break;
        }
        memcpy(data + wp, buf + total, n);
        if (wp >= rp) {
            vas_store(blk, d + VAS_RING_WRAP_POS, wp + n);
        }
        wp = (wp + n) % TO_HOST_SIZE;
        total += n;
    }
    vas_store(blk, d + VAS_RING_WRITE_POS, wp);
    return total;
}

/* Guest consumer on the from-host ring. */
static size_t guest_read(uint8_t *buf, size_t len)
{
    size_t d = VAS_FROM_HOST, total = 0;
    uint8_t *data = blk + VAS_HEADER_SIZE;
    uint32_t wp = vas_load(blk, d + VAS_RING_WRITE_POS);
    uint32_t wrap = vas_load(blk, d + VAS_RING_WRAP_POS);
    uint32_t rp = vas_load(blk, d + VAS_RING_READ_POS);

    while (total < len && rp != wp) {
        uint32_t end = wp >= rp ? wp : wrap;
        size_t n = MIN(end - rp, len - total);
        memcpy(buf + total, data + rp, n);
        total += n;
        rp += n;
        if (wp < rp && rp == wrap) {
            rp = 0;
        }
    }
    vas_store(blk, d + VAS_RING_READ_POS, rp);
    return total;
}

/* Push a long pattern through a ring in odd-sized pieces; check the order. */
static void stream(size_t (*put)(const uint8_t *, size_t),
                   size_t (*get)(uint8_t *, size_t))
{
    uint8_t in[4096], out[4096];
    size_t sent = 0, got = 0, step = 0;

    for (size_t i = 0; i < sizeof(in); i++) {
        in[i] = i * 31 + 7;
    }
    while (got < sizeof(in)) {
        size_t w = 1 + (step * 7) % 50, r = 1 + (step * 13) % 45;
        sent += put(in + sent, MIN(w, sizeof(in) - sent));
        got += get(out + got, r);
        g_assert_cmpuint(got, <=, sent);
        g_assert(++step < 100000);
    }
    g_assert_cmpmem(in, sizeof(in), out, sizeof(out));
}

static size_t host_read(uint8_t *buf, size_t len)
{
    ssize_t n = vas_ring_read(blk, BLK_SIZE, buf, len);
    g_assert_cmpint(n, >=, 0);
    return n;
}

static size_t host_write(const uint8_t *buf, size_t len)
{
    ssize_t n = vas_ring_write(blk, BLK_SIZE, buf, len);
    g_assert_cmpint(n, >=, 0);
    return n;
}

static void test_to_host(void)
{
    setup();
    stream(guest_write, host_read);
}

static void test_from_host(void)
{
    setup();
    stream(host_write, guest_read);
}

static void test_never_overfills(void)
{
    uint8_t buf[256] = { 0 };

    setup();
    g_assert_cmpint(vas_ring_room(blk, BLK_SIZE), ==, FROM_HOST_SIZE - 1);
    g_assert_cmpint(vas_ring_write(blk, BLK_SIZE, buf, sizeof(buf)), ==,
                    FROM_HOST_SIZE - 1);
    g_assert_cmpint(vas_ring_room(blk, BLK_SIZE), ==, 0);
    g_assert_cmpint(vas_ring_write(blk, BLK_SIZE, buf, 1), ==, 0);
    /* reader frees 10 bytes, writer wraps into them */
    vas_store(blk, VAS_FROM_HOST + VAS_RING_READ_POS, 10);
    g_assert_cmpint(vas_ring_room(blk, BLK_SIZE), ==, 10);
    g_assert_cmpint(vas_ring_write(blk, BLK_SIZE, buf, sizeof(buf)), ==, 10);
}

static void test_rejects_bad_header(void)
{
    uint8_t buf[8];

    /* ring past the end of the block */
    setup();
    vas_store(blk, VAS_TO_HOST + VAS_RING_SIZE, BLK_SIZE);
    g_assert_cmpint(vas_ring_read(blk, BLK_SIZE, buf, 8), ==, -1);
    /* data offset inside the header page */
    setup();
    vas_store(blk, VAS_FROM_HOST + VAS_RING_DATA_OFFSET, 0x10);
    g_assert_cmpint(vas_ring_write(blk, BLK_SIZE, buf, 8), ==, -1);
    /* positions out of range, including a huge wrap_pos */
    setup();
    vas_store(blk, VAS_TO_HOST + VAS_RING_WRITE_POS, TO_HOST_SIZE);
    g_assert_cmpint(vas_ring_read(blk, BLK_SIZE, buf, 8), ==, -1);
    setup();
    vas_store(blk, VAS_TO_HOST + VAS_RING_READ_POS, 8);
    vas_store(blk, VAS_TO_HOST + VAS_RING_WRAP_POS, 0xffffffff);
    g_assert_cmpint(vas_ring_read(blk, BLK_SIZE, buf, 8), ==, -1);
    setup();
    vas_store(blk, VAS_FROM_HOST + VAS_RING_READ_POS, 0x80000000);
    g_assert_cmpint(vas_ring_write(blk, BLK_SIZE, buf, 8), ==, -1);
    /* block smaller than the header */
    setup();
    g_assert_cmpint(vas_ring_read(blk, 0x800, buf, 8), ==, -1);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/vaddress-ring/to-host", test_to_host);
    g_test_add_func("/vaddress-ring/from-host", test_from_host);
    g_test_add_func("/vaddress-ring/never-overfills", test_never_overfills);
    g_test_add_func("/vaddress-ring/rejects-bad-header",
                    test_rejects_bad_header);
    return g_test_run();
}
