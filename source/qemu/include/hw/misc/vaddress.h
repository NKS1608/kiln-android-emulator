/*
 * vaddress sub-device interface (kiln).
 *
 * A sub-device instance is created by the guest's BIND command on a mapped
 * block and lives until TERMINATE, unmap or reset. All callbacks run with the
 * BQL held, in the vCPU thread that touched the device, so block memory stays
 * valid for their whole duration.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_VADDRESS_H
#define HW_MISC_VADDRESS_H

typedef struct VAddressState VAddressState;
typedef struct VAddressSubDevice VAddressSubDevice;

typedef struct VAddressSubDeviceOps {
    uint32_t type;
    /* Optional. New instance on sd->mem; false refuses the BIND. */
    bool (*bind)(VAddressSubDevice *sd);
    /* Optional. The instance moves to a bigger block (BIND with an old block); BQL held. Must switch sd->mem and
     * sd->size atomically with respect to any thread using them, and not touch the old memory after returning. */
    void (*rebind)(VAddressSubDevice *sd, uint8_t *mem, uint64_t size);
    /* Guest rang the doorbell. */
    void (*notify)(VAddressSubDevice *sd);
    /* Optional. Instance is going away; free sd->opaque. */
    void (*unbind)(VAddressSubDevice *sd);
} VAddressSubDeviceOps;

struct VAddressSubDevice {
    VAddressState *dev;
    const VAddressSubDeviceOps *ops;
    void *opaque;
    uint32_t id;      /* doorbell value */
    uint32_t pin;     /* interrupt pin index, 0..31 */
    /* Host view of the bound block. Changes when the guest moves the
     * instance to a bigger block (BIND with an old block). */
    uint8_t *mem;
    uint64_t size;
    int fd;           /* shared memory behind mem (renderd mode), else -1 */
};

/* Wake the guest threads sleeping on this instance's pin. */
void vaddress_raise_pin(VAddressSubDevice *sd);

/* Map/unmap host memory (gfxstream's vm ops) at a block the guest ALLOCATEd; BQL held. own (may be NULL) is a mapping
 * of own_size bytes holding hva that QEMU unmaps once the guest no longer sees it; it stays the caller's on failure. */
bool vaddress_host_map(uint64_t gpa, void *hva, uint64_t size, void *own, uint64_t own_size);
void vaddress_host_unmap(uint64_t gpa, uint64_t size);

/* Sub-device 0x9c08 (guest vaddress_sample): byte-stream loopback. */
extern const VAddressSubDeviceOps vaddress_echo_ops;

/* Guest GL and Vulkan streams: upstream gfxstream protocol, served by libkiln_gfx once loaded. */
#define VADDRESS_SUBDEV_GFXSTREAM 0x6701
extern const VAddressSubDeviceOps vaddress_gfxstream_ops;
bool vaddress_gl_init(DeviceState *dev, const char *path, uint32_t width,
                      uint32_t height, Error **errp);
/* The same streams served by kiln-renderd, a separate process, over the socket at path (kiln-renderd.h). */
bool vaddress_renderd_init(DeviceState *dev, const char *path, uint32_t width,
                           uint32_t height, Error **errp);

/* GPU fence timeline for the guest's native sync (vaddress.md, "Sync sub-device"); served by libkiln_gfx too. */
#define VADDRESS_SUBDEV_SYNC 0x5f01
extern const VAddressSubDeviceOps vaddress_sync_ops;

#endif
