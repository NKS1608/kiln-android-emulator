# Out-of-tree build driven by the Android kernel build (TARGET_KERNEL_EXT_MODULES, see vendor/lineage/build/tasks/kernel.mk).
# M is the module directory as an absolute path: the kernel runs from its O= directory and does not resolve a relative M.
KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build
modules modules_install clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(CURDIR) $@
