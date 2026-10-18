/*
 * vaddress sub-device 0x9c08: echoes the to-host stream back to the guest.
 * Test vehicle for the sub-device plumbing. Not what the guest's
 * vaddress_sample benchmark wants: it expects every u32 back incremented by
 * one.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/misc/vaddress.h"
#include "hw/misc/vaddress-ring.h"

/*
 * ponytail: only echoes what fits in the from-host ring right now; the guest
 * reader does not ring the doorbell, so a writer that fills both rings before
 * reading stalls until its next write. Add a host thread if that matters.
 */
static void echo_notify(VAddressSubDevice *sd)
{
    uint8_t buf[4096];
    bool moved = false;

    for (;;) {
        ssize_t room = vas_ring_room(sd->mem, sd->size);
        ssize_t n = room <= 0 ? room :
            vas_ring_read(sd->mem, sd->size, buf, MIN(room, sizeof(buf)));
        if (n < 0) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "vaddress: echo %u: corrupt ring header\n", sd->id);
            return;
        }
        if (n == 0) {
            break;
        }
        vas_ring_write(sd->mem, sd->size, buf, n);
        moved = true;
    }
    if (moved && vas_guest_waiting(sd->mem)) {
        vaddress_raise_pin(sd);
    }
}

const VAddressSubDeviceOps vaddress_echo_ops = {
    .type = 0x9c08,
    .notify = echo_notify,
};
