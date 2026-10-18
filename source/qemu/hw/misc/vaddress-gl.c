/*
 * vaddress sub-device 0x6701: the guest's GL and Vulkan streams (upstream gfxstream protocol).
 *
 * The bytes are handed unchanged to libkiln_gfx (kiln/gfx), the bridge to the gfxstream host
 * renderer. The library is dlopen'ed from the vaddress "gfx-lib" property; without it, BINDs are
 * refused. Posted frames are shown on a QEMU graphic console.
 *
 * With the "renderd" property the renderer runs in kiln-renderd, a process of its own: the stream
 * blocks are shared memory that renderd maps and pumps itself, and only small notifications cross
 * the socket (kiln-renderd.h, docs/PLAN-RENDERD.md). Frames then reach the player directly.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "qemu/thread.h"
#include "qemu/timer.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/sockets.h"
#include "ui/console.h"
#include "ui/surface.h"
#include "hw/misc/vaddress.h"
#include "hw/misc/vaddress-ring.h"
#include "hw/misc/kiln-renderd.h"
#include <gmodule.h>
#include <mach/mach_vm.h>
#include <servers/bootstrap.h>

/* C ABI of libkiln_gfx; source of truth is kiln/gfx/kiln_gfx.h */
typedef struct kiln_conn kiln_conn;
typedef void (*kiln_post_fn)(void *opaque, int width, int height,
                            const uint8_t *pixels);
typedef void (*kiln_wake_fn)(void *opaque);
typedef void (*kiln_map_fn)(uint64_t gpa, void *hva, uint64_t size);
typedef void (*kiln_unmap_fn)(uint64_t gpa, uint64_t size);
typedef void (*kiln_sync_inc_fn)(uint32_t howmuch);

static struct {
    int (*init)(int width, int height, kiln_post_fn post, void *opaque);
    kiln_conn *(*conn_new)(kiln_wake_fn wake, void *opaque);
    int (*conn_write)(kiln_conn *c, const uint8_t *data, size_t len);
    size_t (*conn_pending)(kiln_conn *c);
    size_t (*conn_read)(kiln_conn *c, uint8_t *buf, size_t len);
    void (*conn_free)(kiln_conn *c);
    void (*set_vm_ops)(kiln_map_fn map, kiln_unmap_fn unmap);
    void (*sync_set_inc)(kiln_sync_inc_fn inc);
    void (*sync_set_present)(int present);
    void (*sync_trigger_wait)(uint64_t glsync, uint64_t thread);
} kiln;
static bool gl_ready;

/* Called from renderer threads: take the BQL for the memory-region changes. */
static void gl_vm_map(uint64_t gpa, void *hva, uint64_t size)
{
    bool had = bql_locked();

    if (!had) {
        bql_lock();
    }
    vaddress_host_map(gpa, hva, size, NULL, 0);
    if (!had) {
        bql_unlock();
    }
}

static void gl_vm_unmap(uint64_t gpa, uint64_t size)
{
    bool had = bql_locked();

    if (!had) {
        bql_lock();
    }
    vaddress_host_unmap(gpa, size);
    if (!had) {
        bql_unlock();
    }
}

#define GL_IO_CHUNK     (64 * 1024)

/* Latest posted frame, handed from a renderer thread to the main loop. */
static QemuConsole *gl_con;
static QemuMutex frame_lock;
static uint8_t *frame;
static int frame_w, frame_h;
static bool frame_new;
static QEMUBH *frame_bh;

static void gl_frame_bh(void *opaque)
{
    DisplaySurface *ds;

    qemu_mutex_lock(&frame_lock);
    if (!frame_new) {
        qemu_mutex_unlock(&frame_lock);
        return;
    }
    ds = qemu_console_surface(gl_con);
    if (!ds || surface_width(ds) != frame_w || surface_height(ds) != frame_h) {
        ds = qemu_create_displaysurface(frame_w, frame_h);
        qemu_console_set_surface(gl_con, ds);
    }
    for (int y = 0; y < frame_h; y++) {
        memcpy((uint8_t *)surface_data(ds) + y * surface_stride(ds),
               frame + (size_t)y * frame_w * 4, (size_t)frame_w * 4);
    }
    frame_new = false;
    qemu_mutex_unlock(&frame_lock);
    qemu_console_update_full(gl_con);
}

/* Renderer thread: copy and let the main loop draw it. */
static void gl_post(void *opaque, int width, int height, const uint8_t *pixels)
{
    size_t len = (size_t)width * height * 4;

    qemu_mutex_lock(&frame_lock);
    if (width != frame_w || height != frame_h) {
        frame = g_realloc(frame, len);
        frame_w = width;
        frame_h = height;
    }
    memcpy(frame, pixels, len);
    frame_new = true;
    qemu_mutex_unlock(&frame_lock);
    qemu_bh_schedule(frame_bh);
}

/*
 * Sub-device 0x5f01: the GPU fence timeline behind the guest's native sync. The guest driver (vaddress.ko) numbers its
 * fences in queue order; the doorbell hands one work item (the host sync object gfxstream made for it) to gfxstream's
 * SyncThread, which counts it as passed once the host GPU is past it. The count goes into the block and the pin wakes
 * the guest, which signals every fence up to it. One instance per VM.
 */
#define SYNC_GLSYNC     0x00
#define SYNC_THREAD     0x08
#define SYNC_COMPLETED  0x10

static VAddressSubDevice *sync_sd;  /* BQL */
static QEMUBH *sync_bh;
static uint64_t sync_completed;     /* atomic: SyncThread workers add, the BH publishes */
static uint64_t sync_base;          /* sync_completed at BIND: a rebooted guest numbers its fences from 1 again */

/* gfxstream's SyncThread workers: a host fence passed. */
static void sync_inc(uint32_t howmuch)
{
    qatomic_add(&sync_completed, howmuch);
    qemu_bh_schedule(sync_bh);
}

static void sync_bh_fn(void *opaque)
{
    if (sync_sd) {
        stq_le_p(sync_sd->mem + SYNC_COMPLETED, qatomic_read(&sync_completed) - sync_base);
        vaddress_raise_pin(sync_sd);
    }
}

static bool sync_bind(VAddressSubDevice *sd)
{
    if (!gl_ready || sync_sd || sd->size < SYNC_COMPLETED + 8) {
        return false;
    }
    sync_sd = sd;
    sync_base = qatomic_read(&sync_completed);
    stq_le_p(sd->mem + SYNC_COMPLETED, 0);
    kiln.sync_set_present(1);
    return true;
}

static void sync_notify(VAddressSubDevice *sd)
{
    kiln.sync_trigger_wait(ldq_le_p(sd->mem + SYNC_GLSYNC), ldq_le_p(sd->mem + SYNC_THREAD));
}

static void sync_unbind(VAddressSubDevice *sd)
{
    kiln.sync_set_present(0);
    sync_sd = NULL;
}

const VAddressSubDeviceOps vaddress_sync_ops = {
    .type = VADDRESS_SUBDEV_SYNC,
    .bind = sync_bind,
    .notify = sync_notify,
    .unbind = sync_unbind,
};

static const GraphicHwOps gl_console_ops = {};

bool vaddress_gl_init(DeviceState *dev, const char *path, uint32_t width,
                      uint32_t height, Error **errp)
{
    static const char *const names[] = {
        "kiln_init", "kiln_conn_new", "kiln_conn_write",
        "kiln_conn_pending", "kiln_conn_read", "kiln_conn_free", "kiln_set_vm_ops",
        "kiln_sync_set_inc", "kiln_sync_set_present", "kiln_sync_trigger_wait",
    };
    gpointer *slots[] = {
        (gpointer *)&kiln.init, (gpointer *)&kiln.conn_new,
        (gpointer *)&kiln.conn_write, (gpointer *)&kiln.conn_pending,
        (gpointer *)&kiln.conn_read, (gpointer *)&kiln.conn_free,
        (gpointer *)&kiln.set_vm_ops,
        (gpointer *)&kiln.sync_set_inc, (gpointer *)&kiln.sync_set_present,
        (gpointer *)&kiln.sync_trigger_wait,
    };
    GModule *mod = g_module_open(path, G_MODULE_BIND_LOCAL);

    if (!mod) {
        error_setg(errp, "vaddress: cannot load gfx-lib '%s': %s", path,
                   g_module_error());
        return false;
    }
    for (size_t i = 0; i < ARRAY_SIZE(names); i++) {
        if (!g_module_symbol(mod, names[i], slots[i])) {
            error_setg(errp, "vaddress: %s: missing symbol %s", path,
                       names[i]);
            g_module_close(mod);
            return false;
        }
    }
    g_module_make_resident(mod);

    kiln.set_vm_ops(gl_vm_map, gl_vm_unmap);
    sync_bh = qemu_bh_new(sync_bh_fn, NULL);
    kiln.sync_set_inc(sync_inc);
    qemu_mutex_init(&frame_lock);
    frame_bh = qemu_bh_new(gl_frame_bh, NULL);
    gl_con = qemu_graphic_console_create(dev, 0, &gl_console_ops, NULL);
    if (!kiln.init(width, height, gl_post, NULL)) {
        error_setg(errp, "vaddress: GL renderer failed to start (see stderr)");
        return false;
    }
    gl_ready = true;
    return true;
}

/*
 * One poller thread per stream: while the stream is active the thread watches
 * both rings itself and publishes host_state with bit 0 set, so the guest skips the doorbell (a VM
 * exit plus a hop through the main loop). When it has been idle for a moment it publishes a new even
 * host_state and sleeps; the next guest write then rings exactly once (vaddress-guest.md,
 * "Notification protocol"). The main loop is only used to raise the interrupt pin, and only when a
 * guest thread is actually asleep.
 */
#define GL_SPIN_NS      100000   /* keep polling this long after the last byte moved */
#define GL_SLEEP_MS     2        /* re-check even without a kick: belt and braces */

typedef struct GLStream {
    VAddressSubDevice *sd;
    kiln_conn *conn;
    QEMUBH *wake_bh;     /* raise the pin from the main loop */
    QemuThread thread;
    QemuMutex lock;
    QemuCond cond;
    QemuMutex ring_lock; /* held while the poller uses sd->mem/size; gl_rebind takes it to switch them */
    bool kick;           /* doorbell or host reply since the poller last looked */
    bool stop;
    bool broken;
    uint32_t sleep_gen;  /* idle host_state values: even, nonzero, never reused */
    uint8_t *buf;
} GLStream;

static void gl_wake_bh(void *opaque)
{
    GLStream *st = opaque;

    vaddress_raise_pin(st->sd);
}

static void gl_kick(GLStream *st)
{
    qemu_mutex_lock(&st->lock);
    st->kick = true;
    qemu_cond_signal(&st->cond);
    qemu_mutex_unlock(&st->lock);
}

/* Move bytes both ways; no BQL. Returns whether anything moved. */
static bool gl_pump_locked(GLStream *st);

static bool gl_pump(GLStream *st)
{
    bool moved;

    qemu_mutex_lock(&st->ring_lock);
    moved = gl_pump_locked(st);
    qemu_mutex_unlock(&st->ring_lock);
    return moved;
}

/* Publish host_state in the ring the instance has now. */
static void gl_store_state(GLStream *st, uint32_t v)
{
    qemu_mutex_lock(&st->ring_lock);
    vas_store(st->sd->mem, VAS_HOST_STATE, v);
    qemu_mutex_unlock(&st->ring_lock);
}

static bool gl_pump_locked(GLStream *st)
{
    VAddressSubDevice *sd = st->sd;
    bool in = false, out = false;
    ssize_t n;

    while ((n = vas_ring_read(sd->mem, sd->size, st->buf, GL_IO_CHUNK)) > 0) {
        in = true;
        if (!st->broken && !kiln.conn_write(st->conn, st->buf, n)) {
            st->broken = true;
            qemu_log_mask(LOG_GUEST_ERROR,
                          "vaddress: gl %u: stream broken, dropping\n", sd->id);
        }
    }
    if (n < 0) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "vaddress: gl %u: corrupt ring header\n", sd->id);
    }

    while (kiln.conn_pending(st->conn)) {
        ssize_t room = vas_ring_room(sd->mem, sd->size);

        if (room < 0) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "vaddress: gl %u: corrupt ring header\n", sd->id);
            break;
        }
        if (room == 0) {
            break; /* the guest reader drains it; we look again on the next pass */
        }
        n = kiln.conn_read(st->conn, st->buf, MIN(room, GL_IO_CHUNK));
        if (n == 0) {
            break;
        }
        vas_ring_write(sd->mem, sd->size, st->buf, n);
        out = true;
    }

    /* Publish the ring positions before looking at what the guest is doing. */
    smp_mb();
    if ((in || out) && vas_guest_waiting(sd->mem)) {
        qemu_bh_schedule(st->wake_bh);
    } else if (!out && vas_reader_blocked(sd->mem) && vas_ring_unread(sd->mem, sd->size)) {
        /* A reader still marked asleep with replies sitting unread: kick it again. */
        static unsigned kicks;

        qemu_bh_schedule(st->wake_bh);
        if (kicks++ < 20) {
            fprintf(stderr, "vaddress: gl %u: safety kick #%u (guest asleep with unread "
                    "replies)\n", sd->id, kicks);
        }
    }
    return in || out;
}

static void *gl_poller(void *opaque)
{
    GLStream *st = opaque;
    int64_t last_move = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);

    gl_store_state(st, 1); /* active: the guest need not ring */
    for (;;) {
        if (gl_pump(st)) {
            last_move = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
            continue;
        }
        qemu_mutex_lock(&st->lock);
        if (st->stop) {
            qemu_mutex_unlock(&st->lock);
            break;
        }
        if (st->kick) {
            st->kick = false;
            qemu_mutex_unlock(&st->lock);
            continue;
        }
        qemu_mutex_unlock(&st->lock);

        if (qemu_clock_get_ns(QEMU_CLOCK_REALTIME) - last_move < GL_SPIN_NS) {
            cpu_relax();
            continue;
        }

        /* Idle: tell the guest to ring on its next write, then re-check for a race. */
        st->sleep_gen += 2;
        gl_store_state(st, st->sleep_gen);
        smp_mb();
        if (gl_pump(st)) {
            gl_store_state(st, 1);
            last_move = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
            continue;
        }
        /*
         * Asleep until a doorbell or a host reply kicks us. The timed wake-up only re-checks the rings
         * (a lost doorbell); with nothing there the stream sleeps on without going active. Waking active
         * on every timeout made each idle stream spin GL_SPIN_NS 500 times a second: with Genshin's 28
         * streams that was 1.3 host cores doing nothing.
         */
        for (;;) {
            bool kicked, stop;

            qemu_mutex_lock(&st->lock);
            if (!st->kick && !st->stop) {
                qemu_cond_timedwait(&st->cond, &st->lock, GL_SLEEP_MS);
            }
            kicked = st->kick;
            stop = st->stop;
            st->kick = false;
            qemu_mutex_unlock(&st->lock);
            if (stop || kicked || gl_pump(st)) {
                break;
            }
        }
        gl_store_state(st, 1);
        last_move = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
    }
    return NULL;
}

/*
 * kiln-renderd transport. Every send happens with the BQL held (sub-device callbacks, the main loop), so the socket
 * needs no lock of its own. Renderd's messages are read in the main loop.
 */
static bool rd_mode;
static int rd_sock = -1;
static GHashTable *rd_streams; /* sub-device id -> VAddressSubDevice; BQL */

/*
 * Renderd died or stopped reading. The VM keeps running without a GPU, so the guest can still be shut down cleanly
 * (the player offers it); its GPU streams just stop. ponytail: telling the guest needs a host-side close in the
 * vaddress driver, and is only worth it with a renderd that can be restarted.
 */
static void rd_lost(const char *why)
{
    error_report("vaddress: kiln-renderd %s: the guest has no GPU any more", why);
    qemu_set_fd_handler(rd_sock, NULL, NULL, NULL);
    close(rd_sock);
    rd_sock = -1;
}

static void rd_send(uint32_t type, uint32_t stream, uint64_t a, uint64_t b, int fd)
{
    KrdMsg m = { .type = type, .stream = stream, .a = a, .b = b };
    g_autofree char *why = NULL;

    if (rd_sock < 0) {
        return; /* renderd is gone (logged when it went) */
    }
    if (krd_send(rd_sock, &m, fd, 0) < 0) {
        why = g_strdup_printf("stopped taking messages (%s)", strerror(errno));
        rd_lost(why);
    }
}

static void rd_sync_set_present(int present)
{
    rd_send(KRD_SYNC_PRESENT, 0, present, 0, -1);
}

static void rd_sync_trigger_wait(uint64_t glsync, uint64_t thread)
{
    rd_send(KRD_SYNC_TRIGGER, 0, glsync, thread, -1);
}

static void rd_read(void *opaque)
{
    char peek;

    /*
     * Everything renderd has sent, not one message per main-loop pass: under load it sends thousands of RAISEs a
     * second, and a full socket makes it drop them. Bounded, so other handlers still run.
     */
    for (int i = 0; i < 256 && rd_sock >= 0; i++) {
        VAddressSubDevice *sd;
        KrdMsg m;
        int fd, r = krd_recv(rd_sock, &m, &fd);

        if (fd >= 0) {
            close(fd); /* no renderd message carries one in this version */
        }
        if (r <= 0) {
            rd_lost(r == 0 ? "closed the connection" : strerror(errno));
            return;
        }
        switch (m.type) {
        case KRD_RAISE:
            sd = g_hash_table_lookup(rd_streams, GUINT_TO_POINTER(m.stream));
            if (sd) { /* else unbound since renderd sent it */
                vaddress_raise_pin(sd);
            }
            break;
        case KRD_SYNC_INC:
            sync_inc(m.a);
            break;
        default:
            error_report_once("vaddress: kiln-renderd sent unknown message %u", m.type);
        }
        if (recv(rd_sock, &peek, 1, MSG_PEEK | MSG_DONTWAIT) <= 0) {
            break; /* nothing more now (or the end, which the next pass reads) */
        }
    }
}

/* Renderd's vm ops (kiln-renderd.h): map its pages into QEMU, then at the guest-physical address. */
static int32_t rd_vm_map(KrdVmMsg *m)
{
    mach_vm_address_t base = 0;
    kern_return_t kr;
    bool ok;

    if (m->body.msgh_descriptor_count != 1 || m->offset + m->size > m->entry_size) {
        error_report("vaddress: kiln-renderd sent a malformed map of gpa 0x%" PRIx64, m->gpa);
        return 0;
    }
    kr = mach_vm_map(mach_task_self(), &base, m->entry_size, 0, VM_FLAGS_ANYWHERE, m->entry.name, 0, false,
                     VM_PROT_READ | VM_PROT_WRITE, VM_PROT_READ | VM_PROT_WRITE, VM_INHERIT_NONE);
    if (kr != KERN_SUCCESS) {
        error_report("vaddress: mapping renderer memory for gpa 0x%" PRIx64 " (0x%" PRIx64 " bytes): %s",
                     m->gpa, m->entry_size, mach_error_string(kr));
        return 0;
    }
    bql_lock();
    ok = vaddress_host_map(m->gpa, (uint8_t *)base + m->offset, m->size, (void *)base, m->entry_size);
    bql_unlock();
    if (!ok) {
        mach_vm_deallocate(mach_task_self(), base, m->entry_size);
    }
    return ok;
}

static void *rd_vm_thread(void *opaque)
{
    mach_port_t port = (mach_port_t)(uintptr_t)opaque;

    for (;;) {
        struct {
            KrdVmMsg m;
            mach_msg_trailer_t trailer;
        } in;
        KrdVmReply r = { .ok = 0 };
        kern_return_t kr = mach_msg(&in.m.h, MACH_RCV_MSG, 0, sizeof(in), port, MACH_MSG_TIMEOUT_NONE,
                                    MACH_PORT_NULL);

        if (kr != KERN_SUCCESS) {
            error_report("vaddress: renderer vm ops port: %s", mach_error_string(kr));
            continue;
        }
        if (in.m.h.msgh_id == KRD_VM_MAP) {
            r.ok = rd_vm_map(&in.m);
        } else if (in.m.h.msgh_id == KRD_VM_UNMAP) {
            bql_lock();
            vaddress_host_unmap(in.m.gpa, in.m.size);
            bql_unlock();
            r.ok = 1;
        } else {
            error_report_once("vaddress: renderer vm ops: unknown message 0x%x", in.m.h.msgh_id);
        }
        if (in.m.body.msgh_descriptor_count) {
            mach_port_deallocate(mach_task_self(), in.m.entry.name); /* mapped (or refused): the right is done */
        }
        r.h.msgh_bits = MACH_MSGH_BITS(MACH_MSGH_BITS_REMOTE(in.m.h.msgh_bits), 0);
        r.h.msgh_size = sizeof(r);
        r.h.msgh_remote_port = in.m.h.msgh_remote_port;
        r.h.msgh_id = in.m.h.msgh_id;
        kr = mach_msg(&r.h, MACH_SEND_MSG, sizeof(r), 0, MACH_PORT_NULL, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
        if (kr != KERN_SUCCESS) {
            error_report_once("vaddress: renderer vm ops reply: %s", mach_error_string(kr));
        }
    }
    return NULL;
}

/* Checks in the vm ops port before HELLO, so renderd finds it when it starts the renderer. */
static bool rd_vm_start(Error **errp)
{
    g_autofree char *name = g_strdup_printf(KRD_VM_SERVICE "%d", (int)getpid());
    static QemuThread thread;
    mach_port_t port;
    kern_return_t kr = bootstrap_check_in(bootstrap_port, name, &port);

    if (kr != KERN_SUCCESS) {
        error_setg(errp, "vaddress: cannot register %s for kiln-renderd: %s", name, bootstrap_strerror(kr));
        return false;
    }
    qemu_thread_create(&thread, "kiln-vm-ops", rd_vm_thread, (void *)(uintptr_t)port, QEMU_THREAD_DETACHED);
    return true;
}

bool vaddress_renderd_init(DeviceState *dev, const char *path, uint32_t width,
                           uint32_t height, Error **errp)
{
    KrdMsg m = { .type = KRD_HELLO, .a = KRD_VERSION, .b = (uint64_t)width << 32 | height };
    int fd, one = 1, sndbuf = 1024 * 1024;
    struct timeval timeout = { .tv_sec = 10 };

    if (!rd_vm_start(errp)) {
        return false;
    }
    rd_sock = unix_connect(path, errp);
    if (rd_sock < 0) {
        error_prepend(errp, "vaddress: kiln-renderd: ");
        return false;
    }
    setsockopt(rd_sock, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
    /*
     * Sends happen with the BQL held: a renderd that stops reading must not freeze the VM. The buffer rides out renderd
     * being busy (an UNBIND waits for the renderer's current command, a shader compile can take seconds); past the
     * timeout renderd counts as lost.
     */
    setsockopt(rd_sock, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
    setsockopt(rd_sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    /* Renderd starts the renderer before it answers, as kiln_init does in process. */
    if (krd_send(rd_sock, &m, -1, 0) < 0 || krd_recv(rd_sock, &m, &fd) <= 0) {
        error_setg_errno(errp, errno, "vaddress: kiln-renderd at %s did not answer", path);
        return false;
    }
    if (m.type != KRD_HELLO || m.a != KRD_VERSION || !m.b) {
        error_setg(errp, "vaddress: kiln-renderd %s", m.type == KRD_HELLO && m.a == KRD_VERSION
                   ? "could not start the renderer (see its log)" : "speaks another protocol version");
        return false;
    }
    rd_mode = true;
    rd_streams = g_hash_table_new(NULL, NULL);
    kiln.sync_set_present = rd_sync_set_present;
    kiln.sync_trigger_wait = rd_sync_trigger_wait;
    sync_bh = qemu_bh_new(sync_bh_fn, NULL);
    /* usb-kbd/usb-tablet name this device's console (display=va0); it never shows a frame in this mode. */
    gl_con = qemu_graphic_console_create(dev, 0, &gl_console_ops, NULL);
    qemu_set_fd_handler(rd_sock, rd_read, NULL, NULL);
    gl_ready = true;
    return true;
}

static bool rd_bind(VAddressSubDevice *sd)
{
    if (rd_sock < 0 || sd->fd < 0) {
        return false;
    }
    g_hash_table_insert(rd_streams, GUINT_TO_POINTER(sd->id), sd);
    rd_send(KRD_BIND, sd->id, sd->size, 0, sd->fd);
    return true;
}

/*
 * Renderd moves over when it reads this. The guest only grows a stream whose rings are empty while the host side
 * sleeps (vaddress-guest.md, "Ring growth"), and its next write rings the doorbell, whose KICK comes after this.
 */
static void rd_rebind(VAddressSubDevice *sd, uint8_t *mem, uint64_t size)
{
    sd->mem = mem;
    sd->size = size;
    rd_send(KRD_REBIND, sd->id, size, 0, sd->fd);
}

static void rd_unbind(VAddressSubDevice *sd)
{
    /* Renderd keeps its own mapping until it reads this, so QEMU may free the block at once. */
    g_hash_table_remove(rd_streams, GUINT_TO_POINTER(sd->id));
    rd_send(KRD_UNBIND, sd->id, 0, 0, -1);
}

/* libkiln_gfx reader thread: a reply is ready */
static void gl_wake(void *opaque)
{
    gl_kick(opaque);
}

static bool gfxstream_bind(VAddressSubDevice *sd)
{
    GLStream *st;

    if (!gl_ready) {
        return false;
    }
    if (rd_mode) {
        return rd_bind(sd);
    }
    st = g_new0(GLStream, 1);
    st->sd = sd;
    st->buf = g_malloc(GL_IO_CHUNK);
    st->wake_bh = qemu_bh_new(gl_wake_bh, st);
    qemu_mutex_init(&st->lock);
    qemu_mutex_init(&st->ring_lock);
    qemu_cond_init(&st->cond);
    st->conn = kiln.conn_new(gl_wake, st);
    sd->opaque = st;
    if (getenv("KILN_NAMES")) {
        /* KILN_NAMES=1: who opens a stream (the guest writes its cmdline into the header). */
        char name[VAS_NAME_SIZE + 1] = {};
        static unsigned seq;

        memcpy(name, sd->mem + VAS_NAME, VAS_NAME_SIZE);
        fprintf(stderr, "vaddress: gl %u bind #%u '%s'\n", sd->id, ++seq, name);
    }
    qemu_thread_create(&st->thread, "kiln-poll", gl_poller, st, QEMU_THREAD_JOINABLE);
    return true;
}

static void gl_rebind(VAddressSubDevice *sd, uint8_t *mem, uint64_t size)
{
    GLStream *st = sd->opaque;

    if (rd_mode) {
        rd_rebind(sd, mem, size);
        return;
    }
    qemu_mutex_lock(&st->ring_lock);
    sd->mem = mem;
    sd->size = size;
    qemu_mutex_unlock(&st->ring_lock);
    gl_kick(st);
}

static void gl_notify(VAddressSubDevice *sd)
{
    if (rd_mode) {
        rd_send(KRD_KICK, sd->id, 0, 0, -1);
        return;
    }
    gl_kick(sd->opaque);
}

static void gl_unbind(VAddressSubDevice *sd)
{
    GLStream *st = sd->opaque;

    if (rd_mode) {
        rd_unbind(sd);
        return;
    }
    /* The poller must stop touching the ring (and the connection) before either goes away. */
    qemu_mutex_lock(&st->lock);
    st->stop = true;
    qemu_cond_signal(&st->cond);
    qemu_mutex_unlock(&st->lock);
    qemu_thread_join(&st->thread);
    kiln.conn_free(st->conn); /* joins its threads: no more gl_wake */
    qemu_bh_delete(st->wake_bh);
    qemu_cond_destroy(&st->cond);
    qemu_mutex_destroy(&st->lock);
    qemu_mutex_destroy(&st->ring_lock);
    g_free(st->buf);
    g_free(st);
}

const VAddressSubDeviceOps vaddress_gfxstream_ops = {
    .type = VADDRESS_SUBDEV_GFXSTREAM,
    .bind = gfxstream_bind,
    .rebind = gl_rebind,
    .notify = gl_notify,
    .unbind = gl_unbind,
};
