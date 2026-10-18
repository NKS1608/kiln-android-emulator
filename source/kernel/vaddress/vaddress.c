// SPDX-License-Identifier: GPL-2.0
/*
 * kiln vaddress: guest driver for the vaddress address-space PCI device
 * (1c87:1074). Userland allocates blocks in BAR2, has the host back them with
 * memory, mmaps them, and binds them to host sub-devices (graphics, services)
 * that talk over byte rings inside the block. See kiln
 * docs/vaddress-guest.md for the device and ioctl protocol.
 */

#include <linux/dma-fence.h>
#include <linux/file.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/sync_file.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

#include "vaddress_uapi.h"

#define VA_VENDOR_ID		0x1c87
#define VA_DEVICE_ID		0x1074

/* BAR0 registers */
#define REG_COMMAND		0x00
#define REG_STATUS		0x04
#define REG_ARG0		0x08
#define REG_ARG1		0x10
#define REG_ARG2		0x18
#define REG_ARG3		0x20
#define REG_ARG4		0x28
#define REG_DOORBELL		0x30

/* BAR1: ring of pending interrupt pins written by the host */
#define CTRL_PIN_BITS		0x00
#define CTRL_PIN_W		0x20
#define CTRL_PIN_R		0x24
#define CTRL_PIN_RING_LEN	32
#define CTRL_MIN_SIZE		0x48

enum {
	CMD_ALLOCATE_BLOCK = 0,
	CMD_DEALLOCATE_BLOCK = 1,
	CMD_MAP_HVA = 2,
	CMD_UNMAP_HVA = 3,
	CMD_BIND_SUB_DEVICE = 5,
	CMD_TERMINATE_SUB_DEVICE = 8,
	CMD_SET_PARAM = 9,
};

#define PARAM_MEM_BAR_GPA	0

/*
 * Sync sub-device: one GPU fence timeline for the whole VM (docs/vaddress.md, "Sync sub-device"). Its block holds the
 * work item the guest hands over with the doorbell, and the count of host fences passed so far, written by the host.
 * Fences are numbered in queue order and signal once the count reaches their number (goldfish sync semantics: host
 * GPU work completes in order).
 */
#define VA_SUBDEV_SYNC		0x5f01
#define SYNC_GLSYNC		0x00	/* u64, guest -> host */
#define SYNC_THREAD		0x08	/* u64, guest -> host */
#define SYNC_COMPLETED		0x10	/* u64, host -> guest */
#define SYNC_BLOCK_SIZE		PAGE_SIZE

struct va_sync {
	void *mem;			/* the bound block, NULL without a sync sub-device */
	u32 id;				/* doorbell value */
	u32 pin_mask;
	u64 context;			/* dma_fence context */
	u64 queued;			/* last fence number handed out */
	struct mutex queue_lock;	/* one work item in the block at a time */
	spinlock_t lock;		/* fence lock and pending list */
	struct list_head pending;
};

struct va_fence {
	struct dma_fence base;		/* first: dma_fence_free() frees the whole struct */
	struct list_head node;
};

struct va_dev {
	struct pci_dev *pdev;
	void __iomem *regs;
	void __iomem *ctrl;
	phys_addr_t mem_phys;
	resource_size_t mem_size;
	struct mutex regs_lock;		/* one command sequence at a time */
	bool param_sent;
	spinlock_t pins_lock;		/* bound blocks, for the IRQ handler */
	struct list_head bound;
	struct va_sync sync;
	struct miscdevice misc;
};

struct va_block {
	struct list_head node;		/* per-file list */
	struct list_head bound_node;	/* va_dev.bound, while bound */
	u64 offset;
	u64 size;
	u64 hva;			/* host cookie from MAP_HVA, 0 if unmapped */
	u64 hva_size;
	u64 hva_hint;
	u32 sub_device;			/* type id, 0 if unbound */
	u32 sub_device_id;		/* doorbell value */
	u32 pin_mask;
	wait_queue_head_t wq;
	bool signaled;			/* latched until the next wait */
	bool dead;			/* freed; wakes and fails waiters */
	struct kref ref;		/* per-file list + sleeping waiters */
};

struct va_file {
	struct va_dev *dev;
	struct mutex lock;
	struct list_head blocks;
};

static struct va_dev *va_the_dev;

/* --- register access (caller holds regs_lock) --- */

static void va_write64(struct va_dev *d, u32 off, u64 v)
{
	writel(lower_32_bits(v), d->regs + off);
	writel(upper_32_bits(v), d->regs + off + 4);
}

static u64 va_read64(struct va_dev *d, u32 off)
{
	u64 lo = readl(d->regs + off);

	return lo | ((u64)readl(d->regs + off + 4) << 32);
}

/* Returns 0 or a negative errno built from the host STATUS. */
static int va_run(struct va_dev *d, u32 cmd)
{
	u32 status;

	writel(cmd, d->regs + REG_COMMAND);
	status = readl(d->regs + REG_STATUS);
	return status ? -(int)min_t(u32, status, MAX_ERRNO) : 0;
}

/* --- sync fences --- */

static const char *va_fence_driver_name(struct dma_fence *f)
{
	return "vaddress";
}

static const char *va_fence_timeline_name(struct dma_fence *f)
{
	return "kiln-gpu";
}

static const struct dma_fence_ops va_fence_ops = {
	.get_driver_name = va_fence_driver_name,
	.get_timeline_name = va_fence_timeline_name,
};

/* Signals every pending fence the host has passed. IRQ context. */
static void va_sync_signal(struct va_sync *s)
{
	struct va_fence *f, *tmp;
	unsigned long flags;
	u64 completed = le64_to_cpu(READ_ONCE(*(__le64 *)(s->mem + SYNC_COMPLETED)));

	spin_lock_irqsave(&s->lock, flags);
	list_for_each_entry_safe(f, tmp, &s->pending, node) {
		if (f->base.seqno > completed)
			break;	/* queue order: the rest are later */
		list_del(&f->node);
		dma_fence_signal_locked(&f->base);
		dma_fence_put(&f->base);
	}
	spin_unlock_irqrestore(&s->lock, flags);
}

static long va_sync_queue_work(struct va_dev *d, void __user *argp)
{
	struct va_sync *s = &d->sync;
	struct va_ioctl_sync_queue_work a;
	struct sync_file *sf;
	struct va_fence *f;
	unsigned long flags;
	int fd;

	if (!s->mem)
		return -ENODEV;
	if (copy_from_user(&a, argp, sizeof(a)))
		return -EFAULT;
	f = kzalloc(sizeof(*f), GFP_KERNEL);
	if (!f)
		return -ENOMEM;
	fd = get_unused_fd_flags(O_CLOEXEC);
	if (fd < 0) {
		kfree(f);
		return fd;
	}

	mutex_lock(&s->queue_lock);
	dma_fence_init(&f->base, &va_fence_ops, &s->lock, s->context, ++s->queued);
	sf = sync_file_create(&f->base);	/* its own reference */
	if (!sf) {
		mutex_unlock(&s->queue_lock);
		dma_fence_put(&f->base);
		put_unused_fd(fd);
		return -ENOMEM;
	}
	/* pending before the doorbell, so the IRQ that passes it finds it; the list keeps the init reference */
	spin_lock_irqsave(&s->lock, flags);
	list_add_tail(&f->node, &s->pending);
	spin_unlock_irqrestore(&s->lock, flags);
	WRITE_ONCE(*(__le64 *)(s->mem + SYNC_GLSYNC), cpu_to_le64(a.glsync));
	WRITE_ONCE(*(__le64 *)(s->mem + SYNC_THREAD), cpu_to_le64(a.thread));
	wmb();	/* the work item before the doorbell; the host takes it before the MMIO write returns */
	writel(s->id, d->regs + REG_DOORBELL);
	mutex_unlock(&s->queue_lock);

	a.fence_fd = fd;
	if (copy_to_user(argp, &a, sizeof(a))) {
		fput(sf->file);	/* the fence stays pending and signals later; nobody waits on it */
		put_unused_fd(fd);
		return -EFAULT;
	}
	fd_install(fd, sf->file);
	return 0;
}

/* --- interrupts --- */

static irqreturn_t va_interrupt(int irq, void *data)
{
	struct va_dev *d = data;
	struct va_block *b;
	u32 w, r, n, mask = 0;
	unsigned long flags;

	w = readl(d->ctrl + CTRL_PIN_W) % CTRL_PIN_RING_LEN;
	r = readl(d->ctrl + CTRL_PIN_R) % CTRL_PIN_RING_LEN;
	n = (w - r) % CTRL_PIN_RING_LEN;
	if (!n)
		return IRQ_NONE;

	rmb();	/* entries were published before w */
	while (n--) {
		u8 pin = readb(d->ctrl + CTRL_PIN_BITS + r);

		mask |= pin < 32 ? BIT(pin) : ~0u;	/* >= 32: wake everyone */
		r = (r + 1) % CTRL_PIN_RING_LEN;
	}
	writel(r, d->ctrl + CTRL_PIN_R);

	spin_lock_irqsave(&d->pins_lock, flags);
	list_for_each_entry(b, &d->bound, bound_node) {
		if (b->pin_mask & mask) {
			b->signaled = true;
			wake_up_interruptible(&b->wq);
		}
	}
	spin_unlock_irqrestore(&d->pins_lock, flags);
	if (d->sync.pin_mask & mask)
		va_sync_signal(&d->sync);
	return IRQ_HANDLED;
}

/* --- block operations (caller holds f->lock) --- */

static struct va_block *va_find(struct va_file *f, u64 offset)
{
	struct va_block *b;

	list_for_each_entry(b, &f->blocks, node)
		if (b->offset == offset)
			return b;
	return NULL;
}

static void va_unbind_local(struct va_dev *d, struct va_block *b)
{
	unsigned long flags;

	if (!b->sub_device)
		return;
	spin_lock_irqsave(&d->pins_lock, flags);
	list_del_init(&b->bound_node);
	spin_unlock_irqrestore(&d->pins_lock, flags);
	b->sub_device = 0;
	b->sub_device_id = 0;
	b->pin_mask = 0;
}

static void va_terminate(struct va_dev *d, struct va_block *b)
{
	if (!b->sub_device)
		return;
	mutex_lock(&d->regs_lock);
	va_write64(d, REG_ARG0, b->offset);
	writel(b->sub_device, d->regs + REG_ARG1);
	va_run(d, CMD_TERMINATE_SUB_DEVICE);
	mutex_unlock(&d->regs_lock);
	va_unbind_local(d, b);
}

static int va_unmap(struct va_dev *d, struct va_block *b)
{
	int ret;

	if (!b->hva_size)
		return 0;
	mutex_lock(&d->regs_lock);
	va_write64(d, REG_ARG0, b->offset);
	va_write64(d, REG_ARG1, b->hva);
	va_write64(d, REG_ARG2, b->hva_size);
	va_write64(d, REG_ARG3, b->hva_hint);
	ret = va_run(d, CMD_UNMAP_HVA);
	mutex_unlock(&d->regs_lock);
	b->hva = 0;
	b->hva_size = 0;
	return ret;
}

static void va_block_release(struct kref *ref)
{
	kfree(container_of(ref, struct va_block, ref));
}

static void va_free_block(struct va_dev *d, struct va_block *b)
{
	va_terminate(d, b);
	va_unmap(d, b);
	mutex_lock(&d->regs_lock);
	va_write64(d, REG_ARG0, b->offset);
	va_run(d, CMD_DEALLOCATE_BLOCK);
	mutex_unlock(&d->regs_lock);
	list_del(&b->node);
	WRITE_ONCE(b->dead, true);
	wake_up_interruptible_all(&b->wq);
	kref_put(&b->ref, va_block_release);
}

static long va_allocate(struct va_file *f, void __user *argp)
{
	struct va_dev *d = f->dev;
	struct va_ioctl_allocate a;
	struct va_block *b;
	int ret;

	if (copy_from_user(&a, argp, sizeof(a)))
		return -EFAULT;
	b = kzalloc(sizeof(*b), GFP_KERNEL);
	if (!b)
		return -ENOMEM;

	mutex_lock(&d->regs_lock);
	va_write64(d, REG_ARG0, a.size);
	ret = va_run(d, CMD_ALLOCATE_BLOCK);
	if (!ret) {
		b->size = va_read64(d, REG_ARG0);
		b->offset = va_read64(d, REG_ARG1);
	}
	mutex_unlock(&d->regs_lock);
	if (ret) {
		kfree(b);
		return ret;
	}

	INIT_LIST_HEAD(&b->bound_node);
	init_waitqueue_head(&b->wq);
	kref_init(&b->ref);
	list_add(&b->node, &f->blocks);

	a.size = b->size;
	a.offset = b->offset;
	a.phys_addr = d->mem_phys + b->offset;
	if (copy_to_user(argp, &a, sizeof(a))) {
		va_free_block(d, b);
		return -EFAULT;
	}
	return 0;
}

static long va_deallocate(struct va_file *f, void __user *argp)
{
	struct va_ioctl_offset a;
	struct va_block *b;

	if (copy_from_user(&a, argp, sizeof(a)))
		return -EFAULT;
	b = va_find(f, a.offset);
	if (!b)
		return -EINVAL;
	va_free_block(f->dev, b);
	return 0;
}

static long va_map_hva(struct va_file *f, void __user *argp)
{
	struct va_dev *d = f->dev;
	struct va_ioctl_map a;
	struct va_block *b;
	int ret;

	if (copy_from_user(&a, argp, sizeof(a)))
		return -EFAULT;
	b = va_find(f, a.offset);
	if (!b)
		return -EINVAL;
	if (b->hva_size)
		return -EEXIST;
	if (!a.size || a.size > b->size)
		return -EINVAL;

	mutex_lock(&d->regs_lock);
	va_write64(d, REG_ARG0, b->offset);
	va_write64(d, REG_ARG1, a.hva);
	va_write64(d, REG_ARG2, a.size);
	ret = va_run(d, CMD_MAP_HVA);
	if (!ret) {
		b->hva = va_read64(d, REG_ARG1);
		b->hva_size = va_read64(d, REG_ARG2);
		b->hva_hint = a.hva;
	}
	mutex_unlock(&d->regs_lock);
	if (ret)
		return ret;

	a.hva = b->hva;
	a.size = b->hva_size;
	if (copy_to_user(argp, &a, sizeof(a))) {
		va_unmap(d, b);
		return -EFAULT;
	}
	return 0;
}

static long va_unmap_hva(struct va_file *f, void __user *argp)
{
	struct va_ioctl_offset a;
	struct va_block *b;

	if (copy_from_user(&a, argp, sizeof(a)))
		return -EFAULT;
	b = va_find(f, a.offset);
	if (!b || !b->hva_size)
		return -EINVAL;
	va_terminate(f->dev, b);
	return va_unmap(f->dev, b);
}

static long va_bind(struct va_file *f, void __user *argp)
{
	struct va_dev *d = f->dev;
	struct va_ioctl_bind a;
	struct va_block *nb, *ob = NULL;
	unsigned long flags;
	int ret;

	if (copy_from_user(&a, argp, sizeof(a)))
		return -EFAULT;
	nb = va_find(f, a.new_offset);
	if (!nb || !nb->hva_size || nb->sub_device || a.sub_device <= 0)
		return -EFAULT;
	if (a.old_offset != a.new_offset) {
		/* moving an instance to a bigger block (ring growth) */
		ob = va_find(f, a.old_offset);
		if (!ob || !ob->sub_device || ob->sub_device != a.sub_device)
			return -EINVAL;
	}

	mutex_lock(&d->regs_lock);
	va_write64(d, REG_ARG0, nb->offset);
	va_write64(d, REG_ARG1, nb->hva);
	va_write64(d, REG_ARG2, nb->size);
	va_write64(d, REG_ARG3, ob ? ob->offset : nb->offset);
	writel(a.sub_device, d->regs + REG_ARG4);
	ret = va_run(d, CMD_BIND_SUB_DEVICE);
	if (!ret) {
		nb->sub_device_id = readl(d->regs + REG_ARG0);
		nb->pin_mask = readl(d->regs + REG_ARG1);
	}
	mutex_unlock(&d->regs_lock);
	if (ret)
		return ret;

	if (ob)
		va_unbind_local(d, ob);	/* the host moved it; no TERMINATE */
	nb->sub_device = a.sub_device;
	nb->signaled = false;
	spin_lock_irqsave(&d->pins_lock, flags);
	list_add(&nb->bound_node, &d->bound);
	spin_unlock_irqrestore(&d->pins_lock, flags);
	return 0;
}

/* PING: value VA_PING_WAIT sleeps until the block's pin fires; anything else
 * rings the doorbell. Called without f->lock held while sleeping. */
static long va_ping(struct va_file *f, void __user *argp)
{
	struct va_dev *d = f->dev;
	struct va_ioctl_ping a;
	struct va_block *b;
	u32 id;
	long ret;

	if (copy_from_user(&a, argp, sizeof(a)))
		return -EFAULT;

	mutex_lock(&f->lock);
	b = va_find(f, a.offset);
	if (!b || !b->hva_size || !b->sub_device) {
		mutex_unlock(&f->lock);
		return -EFAULT;
	}
	if (a.value != VA_PING_WAIT) {
		id = b->sub_device_id;
		mutex_unlock(&f->lock);
		writel(id, d->regs + REG_DOORBELL);
		return 0;
	}
	/* Another thread may free the block while we sleep: hold a ref. */
	kref_get(&b->ref);
	mutex_unlock(&f->lock);

	ret = wait_event_interruptible(b->wq, READ_ONCE(b->signaled) ||
				       READ_ONCE(b->dead));
	/* Only a completed wait consumes the latch: after -ERESTARTSYS an IRQ that landed since the check must stay for the retry. */
	if (!ret)
		WRITE_ONCE(b->signaled, false);
	if (!ret && READ_ONCE(b->dead))
		ret = -EFAULT;
	kref_put(&b->ref, va_block_release);
	return ret;
}

static long va_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct va_file *f = filp->private_data;
	void __user *argp = (void __user *)arg;
	long ret;

	if (cmd == VA_IOCTL_PING)
		return va_ping(f, argp);
	if (cmd == VA_IOCTL_SYNC_QUEUE_WORK)
		return va_sync_queue_work(f->dev, argp);

	mutex_lock(&f->lock);
	switch (cmd) {
	case VA_IOCTL_ALLOCATE:
		ret = va_allocate(f, argp);
		break;
	case VA_IOCTL_DEALLOCATE:
		ret = va_deallocate(f, argp);
		break;
	case VA_IOCTL_MAP_HVA:
		ret = va_map_hva(f, argp);
		break;
	case VA_IOCTL_UNMAP_HVA:
		ret = va_unmap_hva(f, argp);
		break;
	case VA_IOCTL_BIND:
		ret = va_bind(f, argp);
		break;
	default:
		ret = -ENOTTY;
	}
	mutex_unlock(&f->lock);
	return ret;
}

static int va_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct va_file *f = filp->private_data;
	u64 off = (u64)vma->vm_pgoff << PAGE_SHIFT;
	u64 len = vma->vm_end - vma->vm_start;
	struct va_block *b;
	int ret = -EINVAL;

	mutex_lock(&f->lock);
	list_for_each_entry(b, &f->blocks, node) {
		if (off >= b->offset && off + len <= b->offset + b->size) {
			/* Host RAM appears at this BAR2 range once MAP_HVA runs. */
			ret = remap_pfn_range(vma, vma->vm_start,
					      (f->dev->mem_phys + off) >> PAGE_SHIFT,
					      len, vma->vm_page_prot);
			break;
		}
	}
	mutex_unlock(&f->lock);
	return ret;
}

/* The host needs BAR2's guest-physical address before any block command. */
static int va_send_param(struct va_dev *d)
{
	int ret = 0;

	mutex_lock(&d->regs_lock);
	if (!d->param_sent) {
		writel(PARAM_MEM_BAR_GPA, d->regs + REG_ARG0);
		va_write64(d, REG_ARG0 + 4, d->mem_phys);
		ret = va_run(d, CMD_SET_PARAM);
		d->param_sent = !ret;
	}
	mutex_unlock(&d->regs_lock);
	return ret;
}

/*
 * Binds the VM's sync sub-device on a host-backed block of its own. A host without one (ENODEV) leaves sync off: the
 * guest GL then is not offered native sync and keeps its glFinish fallback.
 */
static void va_sync_setup(struct va_dev *d)
{
	struct va_sync *s = &d->sync;
	u64 offset, size, hva;
	u32 pin_mask = 0;
	int ret;

	mutex_init(&s->queue_lock);
	spin_lock_init(&s->lock);
	INIT_LIST_HEAD(&s->pending);
	s->context = dma_fence_context_alloc(1);
	if (va_send_param(d))
		return;

	mutex_lock(&d->regs_lock);
	va_write64(d, REG_ARG0, SYNC_BLOCK_SIZE);
	ret = va_run(d, CMD_ALLOCATE_BLOCK);
	size = va_read64(d, REG_ARG0);
	offset = va_read64(d, REG_ARG1);
	if (!ret) {
		va_write64(d, REG_ARG0, offset);
		va_write64(d, REG_ARG1, 0);	/* the host allocates */
		va_write64(d, REG_ARG2, size);
		ret = va_run(d, CMD_MAP_HVA);
		hva = va_read64(d, REG_ARG1);
	}
	if (!ret) {
		va_write64(d, REG_ARG0, offset);
		va_write64(d, REG_ARG1, hva);
		va_write64(d, REG_ARG2, size);
		va_write64(d, REG_ARG3, offset);
		writel(VA_SUBDEV_SYNC, d->regs + REG_ARG4);
		ret = va_run(d, CMD_BIND_SUB_DEVICE);
		s->id = readl(d->regs + REG_ARG0);
		pin_mask = readl(d->regs + REG_ARG1);
	}
	mutex_unlock(&d->regs_lock);
	if (ret) {
		/* ponytail: the block stays allocated (one page per boot on an old host); free it if hosts get picky */
		dev_info(&d->pdev->dev, "no sync sub-device (%d): GPU fences off\n", ret);
		return;
	}
	s->mem = devm_memremap(&d->pdev->dev, d->mem_phys + offset, size, MEMREMAP_WB);
	if (IS_ERR(s->mem)) {
		dev_warn(&d->pdev->dev, "sync block not mappable (%ld): GPU fences off\n", PTR_ERR(s->mem));
		s->mem = NULL;
		return;
	}
	WRITE_ONCE(s->pin_mask, pin_mask);	/* last: the IRQ handler looks at the block once this is set */
	dev_info(&d->pdev->dev, "sync sub-device %u bound: GPU fences on\n", s->id);
}

static int va_open(struct inode *inode, struct file *filp)
{
	struct va_dev *d = va_the_dev;
	struct va_file *f;

	if (!d)
		return -ENODEV;
	if (va_send_param(d))
		return -EINVAL;

	f = kzalloc(sizeof(*f), GFP_KERNEL);
	if (!f)
		return -ENOMEM;
	f->dev = d;
	mutex_init(&f->lock);
	INIT_LIST_HEAD(&f->blocks);
	filp->private_data = f;
	return 0;
}

static int va_release(struct inode *inode, struct file *filp)
{
	struct va_file *f = filp->private_data;
	struct va_block *b, *tmp;

	mutex_lock(&f->lock);
	list_for_each_entry_safe(b, tmp, &f->blocks, node)
		va_free_block(f->dev, b);
	mutex_unlock(&f->lock);
	kfree(f);
	return 0;
}

static const struct file_operations va_fops = {
	.owner = THIS_MODULE,
	.open = va_open,
	.release = va_release,
	.unlocked_ioctl = va_ioctl,
	.compat_ioctl = compat_ptr_ioctl,
	.mmap = va_mmap,
};

static int va_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct va_dev *d;
	int ret;

	if (pdev->revision != 0)
		return -ENODEV;
	if (va_the_dev)
		return -EBUSY;	/* one device per VM */

	d = devm_kzalloc(&pdev->dev, sizeof(*d), GFP_KERNEL);
	if (!d)
		return -ENOMEM;
	d->pdev = pdev;
	mutex_init(&d->regs_lock);
	spin_lock_init(&d->pins_lock);
	INIT_LIST_HEAD(&d->bound);

	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;
	/* MSI messages are bus-master writes: without this the host drops them. */
	pci_set_master(pdev);
	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (ret)
		return ret;
	if (!(pci_resource_flags(pdev, 1) & IORESOURCE_MEM) ||
	    pci_resource_len(pdev, 1) < CTRL_MIN_SIZE)
		return -ENODEV;
	ret = pcim_iomap_regions(pdev, BIT(0) | BIT(1), "vaddress");
	if (ret)
		return ret;
	d->regs = pcim_iomap_table(pdev)[0];
	d->ctrl = pcim_iomap_table(pdev)[1];
	d->mem_phys = pci_resource_start(pdev, 2);
	d->mem_size = pci_resource_len(pdev, 2);

	/* Device-managed via pcim_enable_device(), like the devm IRQ below. */
	ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_MSI | PCI_IRQ_INTX);
	if (ret < 0)
		return ret;
	ret = devm_request_irq(&pdev->dev, pci_irq_vector(pdev, 0), va_interrupt,
			       IRQF_SHARED, "vaddress", d);
	if (ret)
		return ret;

	d->misc.minor = MISC_DYNAMIC_MINOR;
	d->misc.name = "vaddress";
	d->misc.fops = &va_fops;
	d->misc.mode = 0666;
	ret = misc_register(&d->misc);
	if (ret)
		return ret;

	pci_set_drvdata(pdev, d);
	va_sync_setup(d);	/* before the device is visible, so no GL process starts without it */
	va_the_dev = d;
	dev_info(&pdev->dev, "BAR2 %pa (%llu MiB)\n", &d->mem_phys,
		 (unsigned long long)d->mem_size >> 20);
	return 0;
}

static void va_remove(struct pci_dev *pdev)
{
	struct va_dev *d = pci_get_drvdata(pdev);

	misc_deregister(&d->misc);
	va_the_dev = NULL;
}

static const struct pci_device_id va_ids[] = {
	{ PCI_DEVICE(VA_VENDOR_ID, VA_DEVICE_ID) },
	{ }
};
MODULE_DEVICE_TABLE(pci, va_ids);

static struct pci_driver va_driver = {
	.name = "vaddress",
	.id_table = va_ids,
	.probe = va_probe,
	.remove = va_remove,
};
module_pci_driver(va_driver);

MODULE_DESCRIPTION("kiln vaddress address-space device");
MODULE_LICENSE("GPL");
