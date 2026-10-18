/*
 * Control protocol between QEMU's vaddress device and kiln-renderd, the renderer in its own process
 * (docs/PLAN-RENDERD.md). One Unix stream socket per VM, fixed 24-byte messages; a shared-memory fd rides
 * along with BIND and REBIND (SCM_RIGHTS). The stream bytes never cross the socket: both sides map the
 * same ring block (vaddress-ring.h).
 *
 * Built into QEMU and into kiln-renderd (C++), hence the dual licence.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later OR MIT
 */

#ifndef HW_MISC_KILN_RENDERD_H
#define HW_MISC_KILN_RENDERD_H

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>

#define KRD_VERSION 1

enum {
    /* QEMU -> renderd */
    KRD_HELLO = 1,      /* a = version, b = width << 32 | height. Answered by HELLO {a = version, b = 1 if the renderer started} */
    KRD_BIND,           /* new stream; a = block size, fd = the block */
    KRD_REBIND,         /* the stream moved to a bigger block; a = size, fd = the new block */
    KRD_UNBIND,         /* the stream is gone; renderd stops using its block */
    KRD_KICK,           /* the guest rang the stream's doorbell */
    KRD_SYNC_PRESENT,   /* a = 1 when the guest bound the sync sub-device, 0 when it let go */
    KRD_SYNC_TRIGGER,   /* a = glsync, b = thread: one native sync work item */
    /* renderd -> QEMU */
    KRD_RAISE,          /* raise the stream's interrupt pin */
    KRD_SYNC_INC,       /* a = host fences passed */
};

typedef struct KrdMsg {
    uint32_t type;
    uint32_t stream;    /* the sub-device id, for the stream messages */
    uint64_t a, b;
} KrdMsg;

#ifdef __cplusplus
static_assert(sizeof(KrdMsg) == 24, "KrdMsg is wire format");
#else
_Static_assert(sizeof(KrdMsg) == 24, "KrdMsg is wire format");
#endif

/* Sends one message, with fd unless it is -1; flags as for sendmsg (MSG_DONTWAIT). Returns 0, or -1 with errno set. */
static inline int krd_send(int sock, const KrdMsg *m, int fd, int flags)
{
    union {
        struct cmsghdr h;
        char buf[CMSG_SPACE(sizeof(int))];
    } ctl;
    struct iovec iov = { (void *)m, sizeof(*m) };
    struct msghdr mh;
    ssize_t n;

    memset(&mh, 0, sizeof(mh));
    mh.msg_iov = &iov;
    mh.msg_iovlen = 1;
    if (fd >= 0) {
        memset(&ctl, 0, sizeof(ctl));
        mh.msg_control = ctl.buf;
        mh.msg_controllen = sizeof(ctl.buf);
        CMSG_FIRSTHDR(&mh)->cmsg_level = SOL_SOCKET;
        CMSG_FIRSTHDR(&mh)->cmsg_type = SCM_RIGHTS;
        CMSG_FIRSTHDR(&mh)->cmsg_len = CMSG_LEN(sizeof(int));
        memcpy(CMSG_DATA(CMSG_FIRSTHDR(&mh)), &fd, sizeof(int));
    }
    do {
        n = sendmsg(sock, &mh, flags);
    } while (n < 0 && errno == EINTR);
    if (n == (ssize_t)sizeof(*m)) {
        return 0;
    }
    if (n >= 0) {
        errno = EMSGSIZE; /* a short write of 24 bytes on a stream socket: the peer is going away */
    }
    return -1;
}

/* Receives one message (blocking); *fd is the fd that came with it, or -1. Returns 1, 0 at end of stream, -1 on error. */
static inline int krd_recv(int sock, KrdMsg *m, int *fd)
{
    union {
        struct cmsghdr h;
        char buf[CMSG_SPACE(sizeof(int))];
    } ctl;
    struct iovec iov = { m, sizeof(*m) };
    struct msghdr mh;
    struct cmsghdr *c;
    ssize_t n;

    memset(&mh, 0, sizeof(mh));
    mh.msg_iov = &iov;
    mh.msg_iovlen = 1;
    mh.msg_control = ctl.buf;
    mh.msg_controllen = sizeof(ctl.buf);
    *fd = -1;
    do {
        n = recvmsg(sock, &mh, MSG_WAITALL);
    } while (n < 0 && errno == EINTR);
    for (c = CMSG_FIRSTHDR(&mh); n > 0 && c; c = CMSG_NXTHDR(&mh, c)) {
        if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_RIGHTS) {
            memcpy(fd, CMSG_DATA(c), sizeof(int));
        }
    }
    if (n == (ssize_t)sizeof(*m)) {
        return 1;
    }
    if (n >= 0) {
        return 0; /* end of stream, or a message cut short by it */
    }
    return -1;
}

/*
 * Host-visible memory (gfxstream's vm ops: Vulkan host-visible allocations, GlDirectMem, mapped ColorBuffers). The
 * renderer's pages must appear at a guest-physical address, so QEMU needs them in its own address space. A Mach memory
 * entry shares any range of renderd's memory, Metal's buffers included, so renderd sends one per map over a Mach port
 * QEMU checks in with launchd as KRD_VM_SERVICE "<QEMU pid>" (renderd finds the pid on the socket). Both calls are
 * synchronous, as in process: the guest touches the memory as soon as the renderer replies to it.
 */
#ifdef __APPLE__
#include <mach/mach.h>

#define KRD_VM_SERVICE "kiln.vaddress.vm."

enum {
    KRD_VM_MAP = 0x4b520001,   /* entry = the pages; map gpa..gpa+size to entry + offset */
    KRD_VM_UNMAP,              /* unmap gpa..gpa+size; no descriptor */
};

typedef struct KrdVmMsg {
    mach_msg_header_t h;       /* msgh_id: KRD_VM_MAP or KRD_VM_UNMAP */
    mach_msg_body_t body;
    mach_msg_port_descriptor_t entry;
    uint64_t gpa, size;
    uint64_t offset;           /* where the mapped pointer sits in the entry (which starts on a host page) */
    uint64_t entry_size;
} KrdVmMsg;

typedef struct KrdVmReply {
    mach_msg_header_t h;
    int32_t ok;
} KrdVmReply;
#endif

#endif
