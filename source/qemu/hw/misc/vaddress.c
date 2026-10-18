/*
 * vaddress: address-space PCI device.
 *
 * Written from the behavioral specs in kiln/docs/vaddress.md and
 * kiln/docs/vaddress-guest.md; no vendor code is used.
 *
 * Implemented: identity, the three BARs, the BAR0 register file, block
 * ALLOCATE/DEALLOCATE, MAP_HVA/UNMAP_HVA with host-allocated memory (hint 0),
 * SET_PARAM(MEM_BAR_GPA), sub-devices (BIND/TERMINATE, doorbell, interrupt
 * pins through the BAR1 ring). BIND of a sub-device type with no host
 * implementation is refused, so guest clients fail cleanly instead of
 * blocking on a ring nobody serves.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "hw/pci/pci_device.h"
#include "hw/pci/msi.h"
#include "hw/core/qdev-properties.h"
#include "migration/vmstate.h"
#include "hw/misc/vaddress.h"
#include "qom/object.h"

#define TYPE_VADDRESS "vaddress"
OBJECT_DECLARE_SIMPLE_TYPE(VAddressState, VADDRESS)

#define TYPE_VADDRESS_HOSTMEM "vaddress-hostmem"
OBJECT_DECLARE_SIMPLE_TYPE(VAddressHostMem, VADDRESS_HOSTMEM)

#define VADDRESS_VENDOR_ID      0x1c87
#define VADDRESS_DEVICE_ID      0x1074
#define VADDRESS_CLASS_ID       0x0500 /* PCI_CLASS_MEMORY_RAM */

#define VADDRESS_REGS_SIZE      0x40
#define VADDRESS_CONTROL_SIZE   (1 * MiB)
#define VADDRESS_MEM_SIZE       (8 * GiB)

/* BAR0 register offsets */
#define REG_COMMAND             0x00
#define REG_STATUS              0x04
#define REG_ARG0                0x08
#define REG_ARG1                0x10
#define REG_ARG2                0x18
#define REG_ARG3                0x20
#define REG_ARG4                0x28
#define REG_ISR                 0x2c
#define REG_PING_SUB_DEVICE_ID  0x30

/* BAR1: ring of pending interrupt pins (host produces, guest consumes) */
#define CTRL_PIN_BITS           0x00
#define CTRL_PIN_W              0x20
#define CTRL_PIN_R              0x24
#define CTRL_PIN_RING_LEN       32
#define CTRL_PIN_BROADCAST      0xff /* any value >= 32 wakes every waiter */

enum {
    CMD_ALLOCATE_BLOCK = 0,
    CMD_DEALLOCATE_BLOCK = 1,
    CMD_MAP_HVA = 2,
    CMD_UNMAP_HVA = 3,
    CMD_BIND_SUB_DEVICE = 5,
    CMD_TERMINATE_SUB_DEVICE = 8,
    CMD_SET_PARAM = 9,
};

enum {
    PARAM_MEM_BAR_GPA = 0,
    PARAM_IRQ_MODE = 1,
};

/*
 * STATUS values are guest ABI, so use fixed Linux errno numbers rather than
 * the host's <errno.h> (macOS numbers differ). The guest only tests != 0.
 */
#define VA_OK           0
#define VA_ENOMEM       12
#define VA_EEXIST       17
#define VA_ENODEV       19
#define VA_EINVAL       22
#define VA_EOPNOTSUPP   95

/* MAP_HVA hint values with a special meaning. */
#define HVA_HINT_HOST_ALLOC 0
#define HVA_HINT_REMOTE     1

/*
 * MAP_HVA results are opaque to the guest (it only echoes them back), so hand
 * out a cookie derived from the block offset instead of leaking host pointers.
 */
#define HVA_COOKIE_BASE (1ULL << 47)

/*
 * Owner of one host-memory mapping. A separate QOM object so the RAM region
 * can be torn down while the device lives: flatviews hold references to this
 * object, which frees itself (and the RAM, via RCU) when the last one drops.
 */
struct VAddressHostMem {
    Object parent_obj;
    MemoryRegion mr;
    void *own;      /* renderd mode: a mapping behind mr this object unmaps (shm block or renderer pages), else NULL */
    uint64_t own_size;
    int fd;         /* the shm block's fd when own is one, else -1 */
};

typedef struct VAddressBlock {
    uint64_t offset;
    uint64_t size;
    VAddressHostMem *hostmem; /* NULL when not mapped */
    uint64_t map_size;
    bool host_mapped;         /* hostmem is renderer memory (vaddress_host_map), not guest-allocated RAM */
    VAddressSubDevice *sd;    /* NULL when not bound */
} VAddressBlock;

struct VAddressState {
    PCIDevice parent_obj;

    MemoryRegion regs_mr;
    MemoryRegion control_mr;
    MemoryRegion mem_mr;

    uint32_t regs[VADDRESS_REGS_SIZE / 4];
    uint64_t mem_bar_gpa;
    /* ponytail: sorted list with O(n) first-fit; use an interval tree if
     * block counts reach the thousands (heavy Vulkan use). */
    GList *blocks;
    uint32_t next_sd_id;
    uint32_t next_pin;

    /* GL backend (sub-device 0x6701) */
    char *renderd;  /* kiln-renderd's socket: the renderer runs there and stream blocks are shared memory */
    char *gfx_lib;
    uint32_t gfx_width;
    uint32_t gfx_height;
};

static const VAddressSubDeviceOps *const va_sd_types[] = {
    &vaddress_sync_ops,
    &vaddress_echo_ops,
    &vaddress_gfxstream_ops,
};

static uint64_t va_get_u64(VAddressState *s, hwaddr off)
{
    return deposit64(s->regs[off / 4], 32, 32, s->regs[off / 4 + 1]);
}

static void va_set_u64(VAddressState *s, hwaddr off, uint64_t val)
{
    s->regs[off / 4] = val;
    s->regs[off / 4 + 1] = val >> 32;
}

static VAddressBlock *va_find_block(VAddressState *s, uint64_t offset)
{
    for (GList *l = s->blocks; l; l = l->next) {
        VAddressBlock *b = l->data;
        if (b->offset == offset) {
            return b;
        }
    }
    return NULL;
}

static uint32_t va_allocate(VAddressState *s)
{
    uint64_t page = qemu_real_host_page_size();
    uint64_t req = va_get_u64(s, REG_ARG0);
    uint64_t size, start = 0;
    GList *l;

    if (req == 0 || req > VADDRESS_MEM_SIZE) {
        return VA_EINVAL;
    }
    size = ROUND_UP(req, page);

    /* First fit in the gaps between offset-sorted blocks. */
    for (l = s->blocks; l; l = l->next) {
        VAddressBlock *b = l->data;
        if (b->offset - start >= size) {
            break;
        }
        start = b->offset + b->size;
    }
    if (start + size > VADDRESS_MEM_SIZE) {
        return VA_ENOMEM;
    }

    VAddressBlock *nb = g_new0(VAddressBlock, 1);
    nb->offset = start;
    nb->size = size;
    s->blocks = g_list_insert_before(s->blocks, l, nb);

    va_set_u64(s, REG_ARG0, size);
    va_set_u64(s, REG_ARG1, start);
    return VA_OK;
}

static void va_terminate(VAddressBlock *b)
{
    VAddressSubDevice *sd = b->sd;

    if (!sd) {
        return;
    }
    if (sd->ops->unbind) {
        sd->ops->unbind(sd);
    }
    g_free(sd);
    b->sd = NULL;
}

static void va_unmap_block(VAddressState *s, VAddressBlock *b)
{
    if (!b->hostmem) {
        return;
    }
    /* The sub-device must stop touching the memory before it goes away. */
    va_terminate(b);
    memory_region_del_subregion(&s->mem_mr, &b->hostmem->mr);
    object_unparent(OBJECT(b->hostmem));
    b->hostmem = NULL;
    b->map_size = 0;
    b->host_mapped = false;
}

static void va_free_block(VAddressState *s, VAddressBlock *b)
{
    va_unmap_block(s, b);
    s->blocks = g_list_remove(s->blocks, b);
    g_free(b);
}

static uint32_t va_deallocate(VAddressState *s)
{
    VAddressBlock *b = va_find_block(s, va_get_u64(s, REG_ARG0));

    if (!b) {
        return VA_EINVAL;
    }
    va_free_block(s, b);
    return VA_OK;
}

/*
 * Renderd mode: a block's RAM is a POSIX shared memory object, so kiln-renderd can map the same rings. Unlinked at
 * once: only the fds keep it, QEMU's and the one renderd gets.
 */
static bool va_shm_init(VAddressHostMem *hm, uint64_t size)
{
    static unsigned seq;
    char name[32];
    void *p;
    int fd;

    snprintf(name, sizeof(name), "/kiln-va.%d.%u", (int)getpid(), seq++);
    fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd < 0) {
        error_report("vaddress: shm_open %s: %s", name, strerror(errno));
        return false;
    }
    shm_unlink(name);
    if (ftruncate(fd, size) < 0) {
        error_report("vaddress: ftruncate of shared block to 0x%" PRIx64 ": %s", size, strerror(errno));
        close(fd);
        return false;
    }
    p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) {
        error_report("vaddress: mmap of shared block (0x%" PRIx64 " bytes): %s", size, strerror(errno));
        close(fd);
        return false;
    }
    memory_region_init_ram_ptr(&hm->mr, OBJECT(hm), "ram", size, p);
    hm->own = p;
    hm->own_size = size;
    hm->fd = fd;
    return true;
}

/* Flatviews held the last references: no vCPU or sub-device uses the memory any more. */
static void va_hostmem_finalize(Object *obj)
{
    VAddressHostMem *hm = VADDRESS_HOSTMEM(obj);

    if (hm->own) {
        munmap(hm->own, hm->own_size);
        if (hm->fd >= 0) {
            close(hm->fd);
        }
    }
}

static uint32_t va_map_hva(VAddressState *s)
{
    VAddressBlock *b = va_find_block(s, va_get_u64(s, REG_ARG0));
    uint64_t hint = va_get_u64(s, REG_ARG1);
    uint64_t size = ROUND_UP(va_get_u64(s, REG_ARG2),
                             qemu_real_host_page_size());
    g_autofree char *name = NULL;
    Error *err = NULL;
    VAddressHostMem *hm;

    if (!b || size == 0 || size > b->size) {
        return VA_EINVAL;
    }
    if (b->hostmem) {
        return VA_EEXIST;
    }
    if (hint != HVA_HINT_HOST_ALLOC) {
        /*
         * hint 1 is "remote memory", any other value is an existing host VA
         * (Vulkan host-visible memory). The guest must never pick host
         * addresses itself; both need a backend that vouches for the memory.
         */
        qemu_log_mask(LOG_UNIMP, "vaddress: MAP_HVA hint 0x%" PRIx64
                      " not supported\n", hint);
        return VA_EOPNOTSUPP;
    }

    hm = VADDRESS_HOSTMEM(object_new(TYPE_VADDRESS_HOSTMEM));
    name = g_strdup_printf("hostmem[0x%" PRIx64 "]", b->offset);
    if (s->renderd) {
        if (!va_shm_init(hm, size)) {
            object_unref(OBJECT(hm));
            return VA_ENOMEM;
        }
    } else if (!memory_region_init_ram_flags_nomigrate(&hm->mr, OBJECT(hm), "ram",
                                                       size, 0, &err)) {
        error_reportf_err(err, "vaddress: MAP_HVA of 0x%" PRIx64
                          " bytes at 0x%" PRIx64 " failed: ",
                          size, b->offset);
        object_unref(OBJECT(hm));
        return VA_ENOMEM;
    }
    object_property_add_child(OBJECT(s), name, OBJECT(hm));
    object_unref(OBJECT(hm)); /* the child property holds it now */
    memory_region_add_subregion(&s->mem_mr, b->offset, &hm->mr);

    b->hostmem = hm;
    b->map_size = size;
    va_set_u64(s, REG_ARG1, HVA_COOKIE_BASE + b->offset);
    va_set_u64(s, REG_ARG2, size);
    return VA_OK;
}

static VAddressState *va_active; /* the (single) device instance, for host-initiated maps */

/*
 * gfxstream's vkMapMemoryIntoAddressSpace: the host maps a Vulkan host-visible allocation at
 * the guest-physical address of a block the guest ALLOCATEd (and mmapped) earlier. The caller
 * holds the BQL.
 */
bool vaddress_host_map(uint64_t gpa, void *hva, uint64_t size, void *own, uint64_t own_size)
{
    VAddressState *s = va_active;
    VAddressBlock *b;
    VAddressHostMem *hm;
    g_autofree char *name = NULL;

    if (!s || gpa < s->mem_bar_gpa) {
        return false;
    }
    b = va_find_block(s, gpa - s->mem_bar_gpa);
    if (!b || b->hostmem || size == 0 || size > b->size) {
        qemu_log_mask(LOG_GUEST_ERROR, "vaddress: host map of 0x%" PRIx64 " bytes at gpa 0x%"
                      PRIx64 ": no free block there\n", size, gpa);
        return false;
    }
    hm = VADDRESS_HOSTMEM(object_new(TYPE_VADDRESS_HOSTMEM));
    name = g_strdup_printf("vkmem[0x%" PRIx64 "]", b->offset);
    memory_region_init_ram_ptr(&hm->mr, OBJECT(hm), "ram", size, hva);
    hm->own = own;
    hm->own_size = own_size;
    hm->fd = -1;
    object_property_add_child(OBJECT(s), name, OBJECT(hm));
    object_unref(OBJECT(hm)); /* the child property holds it now */
    memory_region_add_subregion(&s->mem_mr, b->offset, &hm->mr);
    b->hostmem = hm;
    b->map_size = size;
    b->host_mapped = true;
    return true;
}

void vaddress_host_unmap(uint64_t gpa, uint64_t size)
{
    VAddressState *s = va_active;
    VAddressBlock *b;

    if (!s || gpa < s->mem_bar_gpa) {
        return;
    }
    b = va_find_block(s, gpa - s->mem_bar_gpa);
    /*
     * The renderer unmaps by address only. If the guest already freed its block and the offset was reused, this must not
     * tear down the new owner (a live GL stream, say): only renderer memory of the size it mapped is ours to remove.
     */
    if (b && b->hostmem && b->host_mapped && b->map_size == size) {
        va_unmap_block(s, b);
    }
}

static uint32_t va_unmap_hva(VAddressState *s)
{
    VAddressBlock *b = va_find_block(s, va_get_u64(s, REG_ARG0));

    if (!b || !b->hostmem) {
        return VA_EINVAL;
    }
    va_unmap_block(s, b);
    return VA_OK;
}

static const VAddressSubDeviceOps *va_sd_ops(uint32_t type)
{
    for (size_t i = 0; i < ARRAY_SIZE(va_sd_types); i++) {
        if (va_sd_types[i]->type == type) {
            return va_sd_types[i];
        }
    }
    return NULL;
}

/*
 * BIND: G08 new block, G32 old block (== new when there is none), G40 type.
 * With an old block the existing instance moves to the new (bigger) block;
 * the guest never TERMINATEs the old one.
 */
static uint32_t va_bind(VAddressState *s)
{
    VAddressBlock *nb = va_find_block(s, va_get_u64(s, REG_ARG0));
    uint64_t old_offset = va_get_u64(s, REG_ARG3);
    uint32_t type = s->regs[REG_ARG4 / 4];
    VAddressSubDevice *sd;
    uint8_t *mem;

    if (!nb || !nb->hostmem || nb->sd) {
        return VA_EINVAL;
    }
    if (old_offset != nb->offset) {
        VAddressBlock *ob = va_find_block(s, old_offset);
        if (!ob || !ob->sd || ob->sd->ops->type != type) {
            return VA_EINVAL;
        }
        sd = ob->sd;
        ob->sd = NULL;
    } else {
        const VAddressSubDeviceOps *ops = va_sd_ops(type);
        if (!ops) {
            qemu_log_mask(LOG_UNIMP, "vaddress: no host side for sub-device"
                          " 0x%x\n", type);
            return VA_ENODEV;
        }
        sd = g_new0(VAddressSubDevice, 1);
        sd->dev = s;
        sd->ops = ops;
        sd->id = ++s->next_sd_id ?: ++s->next_sd_id; /* 0 means unbound */
        /* Spread instances over pins; a shared pin only costs spurious
         * wakeups. */
        sd->pin = s->next_pin++ % CTRL_PIN_RING_LEN;
    }
    mem = memory_region_get_ram_ptr(&nb->hostmem->mr);
    sd->fd = nb->hostmem->own ? nb->hostmem->fd : -1;
    if (old_offset != nb->offset && sd->ops->rebind) {
        sd->ops->rebind(sd, mem, nb->map_size);
    } else {
        sd->mem = mem;
        sd->size = nb->map_size;
    }
    if (old_offset == nb->offset && sd->ops->bind && !sd->ops->bind(sd)) {
        g_free(sd);
        return VA_ENODEV;
    }
    nb->sd = sd;
    s->regs[REG_ARG0 / 4] = sd->id;
    s->regs[REG_ARG1 / 4] = 1u << sd->pin;
    return VA_OK;
}

static uint32_t va_terminate_cmd(VAddressState *s)
{
    VAddressBlock *b = va_find_block(s, va_get_u64(s, REG_ARG0));

    if (!b) {
        return VA_EINVAL;
    }
    va_terminate(b);
    return VA_OK;
}

static void va_doorbell(VAddressState *s, uint32_t id)
{
    for (GList *l = s->blocks; l; l = l->next) {
        VAddressBlock *b = l->data;
        if (b->sd && b->sd->id == id) {
            b->sd->ops->notify(b->sd);
            return;
        }
    }
    qemu_log_mask(LOG_GUEST_ERROR, "vaddress: doorbell for unbound"
                  " sub-device %u\n", id);
}

void vaddress_raise_pin(VAddressSubDevice *sd)
{
    VAddressState *s = sd->dev;
    PCIDevice *pdev = PCI_DEVICE(s);
    uint8_t *ctrl = memory_region_get_ram_ptr(&s->control_mr);
    uint32_t *wp = (uint32_t *)(ctrl + CTRL_PIN_W);
    /* BAR1 is guest RAM: mask whatever is there. */
    uint32_t w = le32_to_cpu(qatomic_read(wp)) % CTRL_PIN_RING_LEN;
    uint32_t r = le32_to_cpu(qatomic_read((uint32_t *)(ctrl + CTRL_PIN_R))) %
                 CTRL_PIN_RING_LEN;
    bool pending = false;

    /*
     * No de-duplication against entries already in the ring: the guest may have
     * handled such an entry without advancing the read index yet, and skipping
     * the push would then leave a freshly sleeping waiter with no wakeup.
     */
    if (!pending) {
        uint32_t nw = (w + 1) % CTRL_PIN_RING_LEN;
        if (nw == r) {
            /* ponytail: ring full (31 other pins pending); turn the newest
             * entry into a broadcast. Lost if the guest is consuming that very
             * entry right now; a per-pin pending bitmap would close it. */
            ctrl[CTRL_PIN_BITS + (w + CTRL_PIN_RING_LEN - 1) %
                 CTRL_PIN_RING_LEN] = CTRL_PIN_BROADCAST;
        } else {
            ctrl[CTRL_PIN_BITS + w] = sd->pin;
            smp_wmb();
            qatomic_set(wp, cpu_to_le32(nw));
        }
    }
    if (msi_enabled(pdev)) {
        msi_notify(pdev, 0);
    } else {
        /* No ack register in ring mode: a level IRQ would storm. */
        pci_set_irq(pdev, 1);
        pci_set_irq(pdev, 0);
    }
}

static uint32_t va_set_param(VAddressState *s)
{
    uint32_t param = s->regs[REG_ARG0 / 4];
    /* value is u64 at 0x0c (lo) / 0x10 (hi), straddling ARG0/ARG1 */
    uint64_t value = va_get_u64(s, REG_ARG0 + 4);

    switch (param) {
    case PARAM_MEM_BAR_GPA:
        s->mem_bar_gpa = value;
        return VA_OK;
    case PARAM_IRQ_MODE:
        /* Only ring mode; refusing keeps the guest on it (as the original). */
        return VA_EOPNOTSUPP;
    default:
        return VA_EINVAL;
    }
}

static uint32_t va_run_command(VAddressState *s, uint32_t cmd)
{
    switch (cmd) {
    case CMD_ALLOCATE_BLOCK:
        return va_allocate(s);
    case CMD_DEALLOCATE_BLOCK:
        return va_deallocate(s);
    case CMD_MAP_HVA:
        return va_map_hva(s);
    case CMD_UNMAP_HVA:
        return va_unmap_hva(s);
    case CMD_BIND_SUB_DEVICE:
        return va_bind(s);
    case CMD_TERMINATE_SUB_DEVICE:
        return va_terminate_cmd(s);
    case CMD_SET_PARAM:
        return va_set_param(s);
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "vaddress: invalid command %u\n", cmd);
        return VA_EINVAL;
    }
}

static uint64_t va_regs_read(void *opaque, hwaddr addr, unsigned size)
{
    VAddressState *s = opaque;

    if (size != 4 || (addr & 3)) {
        return 0;
    }
    if (addr == REG_ISR) {
        return 0; /* PMIO irq mode is never enabled */
    }
    return s->regs[addr / 4];
}

static void va_regs_write(void *opaque, hwaddr addr, uint64_t val,
                          unsigned size)
{
    VAddressState *s = opaque;

    if (size != 4 || (addr & 3)) {
        return;
    }
    s->regs[addr / 4] = val;
    switch (addr) {
    case REG_COMMAND:
        s->regs[REG_STATUS / 4] = va_run_command(s, val);
        break;
    case REG_PING_SUB_DEVICE_ID:
        va_doorbell(s, val);
        break;
    }
}

static const MemoryRegionOps va_regs_ops = {
    .read = va_regs_read,
    .write = va_regs_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    /* Other access sizes are accepted and ignored / read 0. */
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl = { .min_access_size = 1, .max_access_size = 8 },
};

static void va_free_all_blocks(VAddressState *s)
{
    while (s->blocks) {
        va_free_block(s, s->blocks->data);
    }
}

static void va_reset(DeviceState *dev)
{
    VAddressState *s = VADDRESS(dev);

    va_free_all_blocks(s);
    memset(s->regs, 0, sizeof(s->regs));
    s->mem_bar_gpa = 0;
    s->next_sd_id = 0;
    s->next_pin = 0;
    memset(memory_region_get_ram_ptr(&s->control_mr), 0,
           VADDRESS_CONTROL_SIZE);
}

static void va_realize(PCIDevice *pdev, Error **errp)
{
    VAddressState *s = VADDRESS(pdev);
    Error *err = NULL;
    int ret;

    va_active = s;
    pci_config_set_interrupt_pin(pdev->config, 1);
    /* The guest prefers MSI and falls back to INTx without it. */
    ret = msi_init(pdev, 0, 1, true, false, &err);
    if (ret == -ENOTSUP) {
        warn_reportf_err(err, "vaddress: using INTx: ");
    } else if (ret) {
        error_propagate(errp, err);
        return;
    }

    memory_region_init_io(&s->regs_mr, OBJECT(s), &va_regs_ops, s,
                          "vaddress_control", VADDRESS_REGS_SIZE);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->regs_mr);

    if (!memory_region_init_ram_flags_nomigrate(&s->control_mr, OBJECT(s),
                                                "vaddress_control_mem",
                                                VADDRESS_CONTROL_SIZE, 0,
                                                errp)) {
        return;
    }
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY |
                     PCI_BASE_ADDRESS_MEM_PREFETCH, &s->control_mr);

    memory_region_init(&s->mem_mr, OBJECT(s), "vaddress_mem",
                       VADDRESS_MEM_SIZE);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY |
                     PCI_BASE_ADDRESS_MEM_TYPE_64 |
                     PCI_BASE_ADDRESS_MEM_PREFETCH, &s->mem_mr);

    if (s->renderd && s->gfx_lib) {
        error_setg(errp, "vaddress: renderd and gfx-lib are two ways to run the renderer: give one");
    } else if (s->renderd) {
        vaddress_renderd_init(DEVICE(s), s->renderd, s->gfx_width, s->gfx_height, errp);
    } else if (s->gfx_lib) {
        vaddress_gl_init(DEVICE(s), s->gfx_lib, s->gfx_width, s->gfx_height,
                         errp);
    }
}

static void va_exit(PCIDevice *pdev)
{
    va_free_all_blocks(VADDRESS(pdev));
    msi_uninit(pdev);
}

/* ponytail: block state is host-side and dynamic; add a vmstate (blocks +
 * BAR1 + RAM contents) when snapshots/migration are needed. */
static const VMStateDescription vmstate_vaddress = {
    .name = TYPE_VADDRESS,
    .unmigratable = 1,
};

static const Property vaddress_properties[] = {
    /* path to libkiln_gfx; unset = no GL (GL clients are refused) */
    DEFINE_PROP_STRING("gfx-lib", VAddressState, gfx_lib),
    /* or kiln-renderd's socket: the renderer in its own process (docs/PLAN-RENDERD.md) */
    DEFINE_PROP_STRING("renderd", VAddressState, renderd),
    DEFINE_PROP_UINT32("gfx-width", VAddressState, gfx_width, 720),
    DEFINE_PROP_UINT32("gfx-height", VAddressState, gfx_height, 1280),
};

static void va_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = va_realize;
    k->exit = va_exit;
    k->vendor_id = VADDRESS_VENDOR_ID;
    k->device_id = VADDRESS_DEVICE_ID;
    k->revision = 0; /* the guest driver rejects any other revision */
    k->class_id = VADDRESS_CLASS_ID;
    device_class_set_legacy_reset(dc, va_reset);
    dc->vmsd = &vmstate_vaddress;
    device_class_set_props(dc, vaddress_properties);
    dc->desc = "Address space device";
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo vaddress_types[] = {
    {
        .name          = TYPE_VADDRESS,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(VAddressState),
        .class_init    = va_class_init,
        .interfaces    = (const InterfaceInfo[]) {
            { INTERFACE_CONVENTIONAL_PCI_DEVICE },
            { },
        },
    },
    {
        .name          = TYPE_VADDRESS_HOSTMEM,
        .parent        = TYPE_OBJECT,
        .instance_size = sizeof(VAddressHostMem),
        .instance_finalize = va_hostmem_finalize,
    },
};

DEFINE_TYPES(vaddress_types)
