/*
 * End-to-end check of /dev/vaddress against the host echo sub-device (0x9c08).
 * Allocates a block, backs it with host memory, sets up the stream header,
 * binds it, sends bytes, sleeps on the pin until the echo arrives and checks it.
 * Then the sync sub-device: a work item with no host sync object behind it counts as passed at once, so its fence fd
 * must become readable (signaled), and several in a row must all signal.
 *
 * SPDX-License-Identifier: GPL-2.0
 */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "../vaddress_uapi.h"

#define SUB_DEVICE_ECHO	0x9c08
#define HDR		0x1000
#define RING		0x1000	/* each direction */

/* Stream header layout (docs/vaddress-guest.md section 6) */
#define GUEST_READ_STATE	0x044
#define TO_HOST			0x080
#define FROM_HOST		0x100
#define R_DATA_OFFSET		0x00
#define R_SIZE			0x04
#define R_WRITE_POS		0x10
#define R_WRAP_POS		0x14
#define R_READ_POS		0x40

static volatile uint32_t *u32at(uint8_t *blk, size_t off)
{
	return (volatile uint32_t *)(blk + off);
}

#define CHECK(cond, what) do { if (!(cond)) { \
	fprintf(stderr, "FAIL: %s: %s\n", what, strerror(errno)); return 1; } \
	fprintf(stderr, "ok: %s\n", what); } while (0)

int main(void)
{
	struct va_ioctl_allocate al = { .size = HDR + 2 * RING };
	struct va_ioctl_map map = { 0 };
	struct va_ioctl_bind bind = { 0 };
	struct va_ioctl_ping ping = { 0 };
	const char msg[] = "hello from the guest";
	char got[sizeof(msg)] = { 0 };
	uint8_t *blk;
	int fd;

	fd = open("/dev/vaddress", O_RDWR | O_CLOEXEC);
	CHECK(fd >= 0, "open /dev/vaddress");
	CHECK(ioctl(fd, VA_IOCTL_ALLOCATE, &al) == 0, "ALLOCATE");
	printf("block offset 0x%llx size 0x%llx phys 0x%llx\n",
	       (unsigned long long)al.offset, (unsigned long long)al.size,
	       (unsigned long long)al.phys_addr);

	setvbuf(stdout, NULL, _IONBF, 0);
	blk = mmap(NULL, al.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, al.offset);
	CHECK(blk != MAP_FAILED, "mmap");
	map.offset = al.offset;
	map.size = al.size;
	CHECK(ioctl(fd, VA_IOCTL_MAP_HVA, &map) == 0, "MAP_HVA");

	memset(blk, 0, 0x180);
	fprintf(stderr, "ok: header cleared\n");
	*u32at(blk, FROM_HOST + R_DATA_OFFSET) = HDR;
	*u32at(blk, FROM_HOST + R_SIZE) = RING;
	*u32at(blk, TO_HOST + R_DATA_OFFSET) = HDR + RING;
	*u32at(blk, TO_HOST + R_SIZE) = RING;

	bind.new_offset = bind.old_offset = al.offset;
	bind.sub_device = SUB_DEVICE_ECHO;
	CHECK(ioctl(fd, VA_IOCTL_BIND, &bind) == 0, "BIND");

	/* Announce we will sleep, publish the bytes, ring, then wait. */
	*u32at(blk, GUEST_READ_STATE) = 2;
	memcpy(blk + HDR + RING, msg, sizeof(msg));
	*u32at(blk, TO_HOST + R_WRAP_POS) = sizeof(msg);
	__sync_synchronize();
	*u32at(blk, TO_HOST + R_WRITE_POS) = sizeof(msg);
	ping.offset = al.offset;
	ping.value = VA_PING_NOTIFY;
	CHECK(ioctl(fd, VA_IOCTL_PING, &ping) == 0, "PING notify");
	ping.value = VA_PING_WAIT;
	CHECK(ioctl(fd, VA_IOCTL_PING, &ping) == 0, "PING wait");

	CHECK(*u32at(blk, FROM_HOST + R_WRITE_POS) == sizeof(msg), "echo length");
	memcpy(got, blk + HDR, sizeof(msg));
	CHECK(memcmp(got, msg, sizeof(msg)) == 0, "echo bytes");
	printf("echo ok: \"%s\"\n", got);

	struct va_ioctl_offset off = { .offset = al.offset };
	munmap(blk, al.size);
	CHECK(ioctl(fd, VA_IOCTL_DEALLOCATE, &off) == 0, "DEALLOCATE");

	for (int i = 0; i < 3; i++) {
		struct va_ioctl_sync_queue_work work = { .glsync = 0, .thread = 0 };
		struct pollfd pfd = { .events = POLLIN };

		CHECK(ioctl(fd, VA_IOCTL_SYNC_QUEUE_WORK, &work) == 0, "SYNC_QUEUE_WORK");
		pfd.fd = work.fence_fd;
		CHECK(poll(&pfd, 1, 2000) == 1 && (pfd.revents & POLLIN), "fence signaled within 2 s");
		close(work.fence_fd);
	}
	close(fd);
	puts("PASS");
	return 0;
}
