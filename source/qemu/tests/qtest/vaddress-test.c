/*
 * QTest for the vaddress PCI device (kiln).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/host-utils.h"
#include "qemu/bswap.h"
#include "libqtest.h"
#include "libqos/pci.h"
#include "libqos/generic-pcihost.h"
#include "hw/pci/pci_regs.h"
#include "hw/misc/vaddress-ring.h"
#include "hw/misc/kiln-renderd.h"
#include <sys/mman.h>
#include <sys/un.h>
#include <mach/mach_vm.h>
#include <servers/bootstrap.h>

#define REG_COMMAND 0x00
#define REG_STATUS  0x04
#define REG_ARG0    0x08
#define REG_ARG1    0x10
#define REG_ARG2    0x18
#define REG_ARG3    0x20
#define REG_ARG4    0x28
#define REG_DOORBELL 0x30

/* High PCIe MMIO window of the virt machine; the qtest allocator is 32-bit. */
#define BAR2_GPA    0x8000000000ULL

typedef struct {
    QTestState *qts;
    QGenericPCIBus gbus;
    QPCIDevice *dev;
    QPCIBar bar0, bar1;
} VA;

static void save_fn(QPCIDevice *dev, int devfn, void *data)
{
    *(QPCIDevice **)data = dev;
}

static void wr64(VA *va, uint64_t off, uint64_t v)
{
    qpci_io_writel(va->dev, va->bar0, off, v);
    qpci_io_writel(va->dev, va->bar0, off + 4, v >> 32);
}

static uint64_t rd64(VA *va, uint64_t off)
{
    return qpci_io_readl(va->dev, va->bar0, off) |
           (uint64_t)qpci_io_readl(va->dev, va->bar0, off + 4) << 32;
}

static uint32_t cmd(VA *va, uint32_t c)
{
    qpci_io_writel(va->dev, va->bar0, REG_COMMAND, c);
    return qpci_io_readl(va->dev, va->bar0, REG_STATUS);
}

static uint64_t allocate(VA *va, uint64_t size, uint64_t *actual)
{
    wr64(va, REG_ARG0, size);
    g_assert_cmpuint(cmd(va, 0), ==, 0);
    *actual = rd64(va, REG_ARG0);
    return rd64(va, REG_ARG1);
}

static uint32_t map(VA *va, uint64_t off, uint64_t hint, uint64_t size)
{
    wr64(va, REG_ARG0, off);
    wr64(va, REG_ARG1, hint);
    wr64(va, REG_ARG2, size);
    return cmd(va, 2);
}

static uint32_t by_offset(VA *va, uint32_t c, uint64_t off)
{
    wr64(va, REG_ARG0, off);
    return cmd(va, c);
}

static void dev_start_with(VA *va, const char *device)
{
    va->qts = qtest_initf("-machine virt,acpi=off,highmem=on "
                          "-cpu max -m 256 -net none -device %s", device);
    qpci_init_generic(&va->gbus, va->qts, NULL, false);
    qpci_device_foreach(&va->gbus.bus, 0x1c87, 0x1074, save_fn, &va->dev);
    g_assert(va->dev);
    qpci_device_enable(va->dev);
    va->bar0 = qpci_iomap(va->dev, 0, NULL);
    va->bar1 = qpci_iomap(va->dev, 1, NULL);
    qpci_config_writel(va->dev, PCI_BASE_ADDRESS_2, (uint32_t)BAR2_GPA);
    qpci_config_writel(va->dev, PCI_BASE_ADDRESS_3, BAR2_GPA >> 32);
}

static void dev_start(VA *va)
{
    dev_start_with(va, "vaddress");
}

static void dev_stop(VA *va)
{
    g_free(va->dev);
    qtest_quit(va->qts);
}

static void test_vaddress(void)
{
    VA va = { 0 };
    QTestState *qts;
    uint64_t off0, off1, size0, size1;
    uint8_t buf[64], out[64];

    dev_start(&va);
    qts = va.qts;

    /* Identity the guest driver checks. */
    g_assert_cmpuint(qpci_config_readb(va.dev, PCI_REVISION_ID), ==, 0);
    g_assert_cmpuint(qpci_config_readw(va.dev, PCI_CLASS_DEVICE), ==, 0x0500);
    g_assert_cmpuint(qpci_config_readb(va.dev, PCI_INTERRUPT_PIN), ==, 1);

    /* SET_PARAM: value straddles 0x0c..0x13. */
    qpci_io_writel(va.dev, va.bar0, REG_ARG0, 0);
    wr64(&va, REG_ARG0 + 4, BAR2_GPA);
    g_assert_cmpuint(cmd(&va, 9), ==, 0);
    qpci_io_writel(va.dev, va.bar0, REG_ARG0, 1);
    wr64(&va, REG_ARG0 + 4, 1);
    g_assert_cmpuint(cmd(&va, 9), !=, 0);

    /* ALLOCATE rounds to the host page and packs blocks first-fit. */
    off0 = allocate(&va, 0x1234, &size0);
    g_assert_cmpuint(off0, ==, 0);
    g_assert_cmpuint(size0, >=, 0x1234);
    g_assert_cmpuint(size0 % 4096, ==, 0);
    off1 = allocate(&va, 1, &size1);
    g_assert_cmpuint(off1, ==, size0);
    wr64(&va, REG_ARG0, 16ULL << 30);
    g_assert_cmpuint(cmd(&va, 0), !=, 0);

    /* MAP_HVA with host allocation makes RAM appear in BAR2. */
    g_assert_cmpuint(map(&va, off0, 0, 0x1234), ==, 0);
    g_assert_cmpuint(rd64(&va, REG_ARG1), !=, 0);
    g_assert_cmpuint(rd64(&va, REG_ARG2), ==, size0);
    for (int i = 0; i < sizeof(buf); i++) {
        buf[i] = i * 7;
    }
    qtest_memwrite(qts, BAR2_GPA + off0 + 0x100, buf, sizeof(buf));
    qtest_memread(qts, BAR2_GPA + off0 + 0x100, out, sizeof(out));
    g_assert_cmpmem(buf, sizeof(buf), out, sizeof(out));

    /* Refusals: double map, remote/host-VA hints, oversize, unknown block. */
    g_assert_cmpuint(map(&va, off0, 0, 0x1000), !=, 0);
    g_assert_cmpuint(map(&va, off1, 1, 0x1000), !=, 0);
    g_assert_cmpuint(map(&va, off1, 0x12345000, 0x1000), !=, 0);
    g_assert_cmpuint(map(&va, off1, 0, size1 + 1), !=, 0);
    g_assert_cmpuint(map(&va, 0x7777000, 0, 0x1000), !=, 0);

    /* No sub-device backend yet: BIND must fail cleanly. */
    wr64(&va, REG_ARG0, off0);
    qpci_io_writel(va.dev, va.bar0, REG_ARG4, 0x6701);
    g_assert_cmpuint(cmd(&va, 5), !=, 0);

    /* Invalid commands. */
    g_assert_cmpuint(cmd(&va, 4), !=, 0);
    g_assert_cmpuint(cmd(&va, 7), !=, 0);

    /* UNMAP, DEALLOCATE (also of a still-mapped block), then reuse. */
    g_assert_cmpuint(by_offset(&va, 3, off0), ==, 0);
    g_assert_cmpuint(by_offset(&va, 3, off0), !=, 0);
    g_assert_cmpuint(map(&va, off0, 0, size0), ==, 0);
    g_assert_cmpuint(by_offset(&va, 1, off0), ==, 0);
    g_assert_cmpuint(by_offset(&va, 1, off0), !=, 0);
    g_assert_cmpuint(allocate(&va, 0x1000, &size0), ==, 0);
    g_assert_cmpuint(map(&va, 0, 0, size0), ==, 0);

    /* System reset drops every block. */
    qtest_system_reset(qts);
    qpci_device_enable(va.dev);
    va.bar0 = qpci_iomap(va.dev, 0, NULL);
    g_assert_cmpuint(by_offset(&va, 1, 0), !=, 0);

    dev_stop(&va);
}

#define FH 0x400 /* from-host ring size (init daemon) */
#define TH 0x400 /* to-host ring size */

static void wr32(VA *va, uint64_t gpa, uint32_t v)
{
    qtest_writel(va->qts, gpa, v);
}

static uint32_t rd32(VA *va, uint64_t gpa)
{
    return qtest_readl(va->qts, gpa);
}

/* Guest steps 2-4 of a VAddressStream: allocate, map, init the header. */
static uint64_t stream_block(VA *va)
{
    uint64_t total = VAS_HEADER_SIZE + FH + TH, size, off, h;

    off = allocate(va, total, &size);
    g_assert_cmpuint(map(va, off, 0, total), ==, 0);
    h = BAR2_GPA + off;
    wr32(va, h + VAS_TO_HOST + VAS_RING_DATA_OFFSET, VAS_HEADER_SIZE + FH);
    wr32(va, h + VAS_TO_HOST + VAS_RING_SIZE, TH);
    wr32(va, h + VAS_FROM_HOST + VAS_RING_DATA_OFFSET, VAS_HEADER_SIZE);
    wr32(va, h + VAS_FROM_HOST + VAS_RING_SIZE, FH);
    wr32(va, h + VAS_GUEST_WRITE_STATE, 1);
    wr32(va, h + VAS_GUEST_READ_STATE, 1);
    return off;
}

static uint32_t bind_sd(VA *va, uint64_t new_off, uint64_t old_off,
                     uint32_t type, uint32_t *pin_mask)
{
    uint32_t st;

    wr64(va, REG_ARG0, new_off);
    wr64(va, REG_ARG3, old_off);
    qpci_io_writel(va->dev, va->bar0, REG_ARG4, type);
    st = cmd(va, 5);
    *pin_mask = qpci_io_readl(va->dev, va->bar0, REG_ARG1);
    return st ? 0 : qpci_io_readl(va->dev, va->bar0, REG_ARG0);
}

/* Guest writes @msg into the to-host ring and rings the doorbell. */
static void send_msg(VA *va, uint64_t off, uint32_t id, const char *msg)
{
    uint64_t h = BAR2_GPA + off;
    uint32_t wp = rd32(va, h + VAS_TO_HOST + VAS_RING_WRITE_POS);
    size_t n = strlen(msg);

    qtest_memwrite(va->qts, h + VAS_HEADER_SIZE + FH + wp, msg, n);
    wr32(va, h + VAS_TO_HOST + VAS_RING_WRAP_POS, wp + n);
    wr32(va, h + VAS_TO_HOST + VAS_RING_WRITE_POS, wp + n);
    qpci_io_writel(va->dev, va->bar0, REG_DOORBELL, id);
}

static void expect_echo(VA *va, uint64_t off, uint32_t from, const char *msg)
{
    uint64_t h = BAR2_GPA + off;
    size_t n = strlen(msg);
    char got[64] = { 0 };

    g_assert_cmpuint(rd32(va, h + VAS_TO_HOST + VAS_RING_READ_POS), ==,
                     rd32(va, h + VAS_TO_HOST + VAS_RING_WRITE_POS));
    g_assert_cmpuint(rd32(va, h + VAS_FROM_HOST + VAS_RING_WRITE_POS), ==,
                     from + n);
    qtest_memread(va->qts, h + VAS_HEADER_SIZE + from, got, n);
    g_assert_cmpstr(got, ==, msg);
}

static void test_echo(void)
{
    VA va = { 0 };
    uint32_t id, id2, pin, pin2, w;
    uint64_t a, b;

    dev_start(&va);

    /* Unknown sub-device types are refused. */
    a = stream_block(&va);
    g_assert_cmpuint(bind_sd(&va, a, a, 0x6701, &pin), ==, 0);

    /* BIND 0x9c08: doorbell id and a one-bit pin mask come back. */
    id = bind_sd(&va, a, a, 0x9c08, &pin);
    g_assert_cmpuint(id, !=, 0);
    g_assert_cmpuint(ctpop32(pin), ==, 1);
    g_assert_cmpuint(bind_sd(&va, a, a, 0x9c08, &pin2), ==, 0); /* already bound */

    /* Reader asleep: the echo lands and the pin is queued in BAR1. */
    wr32(&va, BAR2_GPA + a + VAS_GUEST_READ_STATE, VAS_READER_BLOCKED);
    send_msg(&va, a, id, "hello");
    expect_echo(&va, a, 0, "hello");
    w = qpci_io_readl(va.dev, va.bar1, 0x20);
    g_assert_cmpuint(w, ==, 1);
    g_assert_cmpuint(qpci_io_readb(va.dev, va.bar1, 0), ==, ctz32(pin));

    /* Same pin still pending: the entry is pushed again (never skipped), so a sleeping guest
     * reader cannot miss a wakeup; the guest drains duplicates. */
    send_msg(&va, a, id, "again");
    expect_echo(&va, a, 5, "again");
    g_assert_cmpuint(qpci_io_readl(va.dev, va.bar1, 0x20), ==, 2);

    /* Ring resize: the instance moves to the new block, same id. */
    b = stream_block(&va);
    id2 = bind_sd(&va, b, a, 0x9c08, &pin2);
    g_assert_cmpuint(id2, ==, id);
    g_assert_cmpuint(pin2, ==, pin);
    g_assert_cmpuint(by_offset(&va, 1, a), ==, 0);
    send_msg(&va, b, id, "moved");
    expect_echo(&va, b, 0, "moved");

    /* TERMINATE: the doorbell no longer reaches anything. */
    g_assert_cmpuint(by_offset(&va, 8, b), ==, 0);
    send_msg(&va, b, id, "gone");
    g_assert_cmpuint(rd32(&va, BAR2_GPA + b + VAS_TO_HOST + VAS_RING_READ_POS),
                     ==, 5);

    /* A corrupt header is rejected without touching host memory. */
    id = bind_sd(&va, b, b, 0x9c08, &pin);
    g_assert_cmpuint(id, !=, 0);
    wr32(&va, BAR2_GPA + b + VAS_TO_HOST + VAS_RING_SIZE, 0x7fffffff);
    send_msg(&va, b, id, "x");

    /* Unmapping a bound block tears the instance down first. */
    g_assert_cmpuint(by_offset(&va, 3, b), ==, 0);
    qpci_io_writel(va.dev, va.bar0, REG_DOORBELL, id);

    dev_stop(&va);
}

/* The test plays kiln-renderd: it listens, and answers QEMU's HELLO while qtest_init waits for the device. */
typedef struct {
    char *path;
    int lst, conn;
} FakeRenderd;

static gpointer fake_renderd_hello(gpointer opaque)
{
    FakeRenderd *fr = opaque;
    KrdMsg m;
    int fd;

    fr->conn = accept(fr->lst, NULL, NULL);
    g_assert_cmpint(fr->conn, >=, 0);
    g_assert_cmpint(krd_recv(fr->conn, &m, &fd), ==, 1);
    g_assert_cmpuint(m.type, ==, KRD_HELLO);
    g_assert_cmpuint(m.a, ==, KRD_VERSION);
    g_assert_cmpuint(m.b, ==, (720ULL << 32) | 1280);
    m.b = 1; /* the renderer started */
    g_assert_cmpint(krd_send(fr->conn, &m, -1, 0), ==, 0);
    return NULL;
}

static KrdMsg expect_msg(FakeRenderd *fr, uint32_t type, uint32_t stream, int *fd)
{
    KrdMsg m;
    int got;

    g_assert_cmpint(krd_recv(fr->conn, &m, &got), ==, 1);
    g_assert_cmpuint(m.type, ==, type);
    g_assert_cmpuint(m.stream, ==, stream);
    if (fd) {
        *fd = got;
    } else {
        g_assert_cmpint(got, ==, -1);
    }
    return m;
}

/* A vm ops call as renderd makes it (kiln-renderd.h): returns QEMU's ok. */
static int32_t vm_call(mach_port_t port, KrdVmMsg *m)
{
    union {
        KrdVmMsg m;
        struct {
            KrdVmReply r;
            mach_msg_trailer_t trailer;
        } reply;
    } buf = { .m = *m };

    buf.m.h.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, MACH_MSG_TYPE_MAKE_SEND_ONCE) | MACH_MSGH_BITS_COMPLEX;
    buf.m.h.msgh_size = sizeof(KrdVmMsg);
    buf.m.h.msgh_remote_port = port;
    buf.m.h.msgh_local_port = mig_get_reply_port();
    g_assert_cmpint(mach_msg(&buf.m.h, MACH_SEND_MSG | MACH_RCV_MSG, sizeof(KrdVmMsg), sizeof(buf),
                             buf.m.h.msgh_local_port, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL), ==, KERN_SUCCESS);
    return buf.reply.r.ok;
}

/* Renderer memory (here the test's own page) appears in BAR2 at a block the guest allocated, both ways. */
static void check_vm_ops(VA *va, FakeRenderd *fr)
{
    g_autofree char *name = NULL;
    mach_port_t port, entry;
    memory_object_size_t esz = vm_page_size;
    socklen_t len = sizeof(pid_t);
    pid_t qemu = 0;
    uint64_t off, size;
    KrdVmMsg m = { 0 };
    char *page, got[16] = { 0 };

    g_assert_cmpint(getsockopt(fr->conn, SOL_LOCAL, LOCAL_PEERPID, &qemu, &len), ==, 0);
    name = g_strdup_printf(KRD_VM_SERVICE "%d", (int)qemu);
    g_assert_cmpint(bootstrap_look_up(bootstrap_port, name, &port), ==, KERN_SUCCESS);

    qpci_io_writel(va->dev, va->bar0, REG_ARG0, 0); /* SET_PARAM(MEM_BAR_GPA) */
    wr64(va, REG_ARG0 + 4, BAR2_GPA);
    g_assert_cmpuint(cmd(va, 9), ==, 0);
    off = allocate(va, vm_page_size, &size);

    page = mmap(NULL, vm_page_size, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
    g_assert(page != MAP_FAILED);
    strcpy(page + 0x40, "renderer");
    g_assert_cmpint(mach_make_memory_entry_64(mach_task_self(), &esz, (mach_vm_offset_t)page,
                                              VM_PROT_READ | VM_PROT_WRITE | MAP_MEM_VM_SHARE, &entry,
                                              MACH_PORT_NULL), ==, KERN_SUCCESS);
    m.h.msgh_id = KRD_VM_MAP;
    m.body.msgh_descriptor_count = 1;
    m.entry = (mach_msg_port_descriptor_t){ .name = entry, .disposition = MACH_MSG_TYPE_MOVE_SEND,
                                            .type = MACH_MSG_PORT_DESCRIPTOR };
    m.gpa = BAR2_GPA + off;
    m.size = vm_page_size;
    m.entry_size = esz;
    g_assert_cmpint(vm_call(port, &m), ==, 1);
    qtest_memread(va->qts, BAR2_GPA + off + 0x40, got, 9);
    g_assert_cmpstr(got, ==, "renderer");
    qtest_memwrite(va->qts, BAR2_GPA + off + 0x80, "guest", 6);
    g_assert_cmpstr(page + 0x80, ==, "guest");

    /* A second map of the same block is refused (QEMU drops the entry it was sent). */
    g_assert_cmpint(mach_make_memory_entry_64(mach_task_self(), &esz, (mach_vm_offset_t)page,
                                              VM_PROT_READ | VM_PROT_WRITE | MAP_MEM_VM_SHARE, &entry,
                                              MACH_PORT_NULL), ==, KERN_SUCCESS);
    m.entry.name = entry;
    g_assert_cmpint(vm_call(port, &m), ==, 0);

    memset(&m, 0, sizeof(m));
    m.h.msgh_id = KRD_VM_UNMAP;
    m.gpa = BAR2_GPA + off;
    m.size = vm_page_size;
    g_assert_cmpint(vm_call(port, &m), ==, 1);
    g_assert_cmpuint(by_offset(va, 1, off), ==, 0); /* the block is free of renderer memory: DEALLOCATE works */
    munmap(page, vm_page_size);
}

static void test_renderd(void)
{
    FakeRenderd fr;
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    g_autofree char *device = NULL;
    VA va = { 0 };
    GThread *t;
    KrdMsg m;
    uint64_t a, b, size;
    uint32_t id, pin, pin2;
    uint8_t *shm;
    char got[8] = { 0 };
    int fd, i;

    fr.path = g_strdup_printf("%s/kiln-renderd-test-%d.sock", g_get_tmp_dir(), (int)getpid());
    g_assert_cmpuint(strlen(fr.path), <, sizeof(addr.sun_path));
    strcpy(addr.sun_path, fr.path);
    unlink(fr.path);
    fr.lst = socket(AF_UNIX, SOCK_STREAM, 0);
    g_assert_cmpint(bind(fr.lst, (struct sockaddr *)&addr, sizeof(addr)), ==, 0);
    g_assert_cmpint(listen(fr.lst, 1), ==, 0);
    t = g_thread_new("fake-renderd", fake_renderd_hello, &fr);
    device = g_strdup_printf("vaddress,renderd=%s", fr.path);
    dev_start_with(&va, device);
    g_thread_join(t);
    check_vm_ops(&va, &fr);

    /* BIND hands renderd the block as shared memory: its mapping sees the header the guest wrote. */
    a = stream_block(&va);
    id = bind_sd(&va, a, a, 0x6701, &pin);
    g_assert_cmpuint(id, !=, 0);
    m = expect_msg(&fr, KRD_BIND, id, &fd);
    g_assert_cmpint(fd, >=, 0);
    size = m.a;
    g_assert_cmpuint(size, >=, VAS_HEADER_SIZE + FH + TH);
    shm = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    g_assert(shm != MAP_FAILED);
    close(fd);
    g_assert_cmpuint(vas_load(shm, VAS_TO_HOST + VAS_RING_SIZE), ==, TH);

    /* The doorbell becomes a KICK; the bytes themselves are already in renderd's view. */
    send_msg(&va, a, id, "hello");
    expect_msg(&fr, KRD_KICK, id, NULL);
    g_assert_cmpint(vas_ring_read(shm, size, got, sizeof(got)), ==, 5);
    g_assert_cmpstr(got, ==, "hello");

    /* Renderd writes a reply into the ring and asks for the pin: QEMU queues it in BAR1. */
    g_assert_cmpint(vas_ring_write(shm, size, "world", 5), ==, 5);
    m = (KrdMsg){ .type = KRD_RAISE, .stream = id };
    g_assert_cmpint(krd_send(fr.conn, &m, -1, 0), ==, 0);
    for (i = 0; i < 2000 && qpci_io_readl(va.dev, va.bar1, 0x20) == 0; i++) {
        g_usleep(1000);
    }
    g_assert_cmpuint(qpci_io_readl(va.dev, va.bar1, 0x20), ==, 1);
    g_assert_cmpuint(qpci_io_readb(va.dev, va.bar1, 0), ==, ctz32(pin));
    expect_echo(&va, a, 0, "world");

    /* Ring growth: the stream moves to a new block, which renderd gets as a new fd. */
    b = stream_block(&va);
    g_assert_cmpuint(bind_sd(&va, b, a, 0x6701, &pin2), ==, id);
    expect_msg(&fr, KRD_REBIND, id, &fd);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    g_assert_cmpuint(by_offset(&va, 1, a), ==, 0); /* renderd's mapping keeps its memory; QEMU's goes */
    g_assert_cmpuint(vas_load(shm, VAS_TO_HOST + VAS_RING_SIZE), ==, TH);

    /* TERMINATE becomes UNBIND. */
    g_assert_cmpuint(by_offset(&va, 8, b), ==, 0);
    expect_msg(&fr, KRD_UNBIND, id, NULL);

    /* Renderd going away leaves QEMU running; new streams are refused. */
    close(fr.conn);
    for (i = 0; i < 2000 && bind_sd(&va, b, b, 0x6701, &pin); i++) {
        by_offset(&va, 8, b);
        g_usleep(1000);
    }
    g_assert_cmpuint(bind_sd(&va, b, b, 0x6701, &pin), ==, 0);

    munmap(shm, size);
    dev_stop(&va);
    close(fr.lst);
    unlink(fr.path);
    g_free(fr.path);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/vaddress/commands", test_vaddress);
    qtest_add_func("/vaddress/echo", test_echo);
    qtest_add_func("/vaddress/renderd", test_renderd);
    return g_test_run();
}
