export ARCH := arm
export CROSS_COMPILE := /usr/local/arm/gcc-linaro-4.9.4-2017.01-x86_64_arm-linux-gnueabihf/bin/arm-linux-gnueabihf-

KERNELDIR := /home/ye/_workspace_imx6ull/linux/linux-imx
NFS_DIR := /home/ye/nfs_shared/rootfs/lib/modules/4.1.15+

ifneq ($(KERNELRELEASE),)
# Called from kernel build system
obj-m := ap3216c.o
else
# Called from command line
PWD := $(shell pwd)
APP_SOURCES := $(wildcard *_app.c)
APP_TARGETS := $(patsubst %_app.c,%_app,$(APP_SOURCES))

build: kernel_modules test_app
	sudo cp *.ko $(NFS_DIR) 2>/dev/null || true
	sudo chmod 777 $(NFS_DIR)/*.ko 2>/dev/null || true
	if [ -n "$(APP_TARGETS)" ]; then sudo cp $(APP_TARGETS) $(NFS_DIR); fi

kernel_modules:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

test_app:
	@for app in $(APP_SOURCES); do \
		target=$$(basename $$app .c); \
		$(CROSS_COMPILE)gcc -o $$target $$app; \
	done

clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean
	rm -f $(APP_TARGETS)

test:
	echo $(CROSS_COMPILE)
	echo $(ARCH)
	ls -la *.c
endif