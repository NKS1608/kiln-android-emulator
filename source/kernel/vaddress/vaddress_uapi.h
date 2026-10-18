/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * kiln /dev/vaddress ioctl interface (shared with guest userland).
 */
#ifndef _UAPI_VADDRESS_H
#define _UAPI_VADDRESS_H

#include <linux/ioctl.h>
#include <linux/types.h>

struct va_ioctl_allocate {
	__u64 size;		/* in: requested; out: host-rounded */
	__u64 offset;		/* out: offset in the mmap space */
	__u64 phys_addr;	/* out: guest-physical address */
};

struct va_ioctl_offset {
	__u64 offset;
};

struct va_ioctl_map {
	__u64 offset;
	__u64 hva;		/* in: 0 = host allocates; out: host cookie */
	__u64 size;		/* in: requested; out: mapped */
};

#define VA_PING_WAIT		0x76	/* sleep until the block's pin fires */
#define VA_PING_NOTIFY		0x86	/* ring the doorbell */

struct va_ioctl_ping {
	__u64 offset;
	__u32 value;
	__u32 pad;
};

struct va_ioctl_bind {
	__u64 new_offset;
	__u64 old_offset;	/* == new_offset when there is no old block */
	__s32 sub_device;	/* host sub-device type id */
	__s32 pad;
};

#define VA_IOCTL_ALLOCATE	_IOWR('L', 0x15, struct va_ioctl_allocate)
#define VA_IOCTL_DEALLOCATE	_IOWR('L', 0x16, struct va_ioctl_offset)
#define VA_IOCTL_MAP_HVA	_IOWR('L', 0x17, struct va_ioctl_map)
#define VA_IOCTL_UNMAP_HVA	_IOWR('L', 0x18, struct va_ioctl_offset)
#define VA_IOCTL_PING		_IOWR('L', 0x19, struct va_ioctl_ping)
/* A sync_file fd that signals when the host GPU passes a host sync object (gfxstream's goldfish_sync_queue_work:
 * glsync and thread as rcCreateSyncKHR returned them). ENODEV when the host has no sync sub-device. */
struct va_ioctl_sync_queue_work {
	__u64 glsync;
	__u64 thread;
	__s32 fence_fd;		/* out */
	__s32 pad;
};

#define VA_IOCTL_BIND		_IOWR('L', 0x1a, struct va_ioctl_bind)
#define VA_IOCTL_SYNC_QUEUE_WORK _IOWR('L', 0x1b, struct va_ioctl_sync_queue_work)

#endif
