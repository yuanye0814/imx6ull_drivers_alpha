export ARCH := arm
export CROSS_COMPILE := /usr/local/arm/gcc-linaro-4.9.4-2017.01-x86_64_arm-linux-gnueabihf/bin/arm-linux-gnueabihf-

KERNELDIR := /home/ye/_workspace_imx6ull/linux/linux-imx
PWD := $(shell pwd)
obj-m := char_dev_base.o

build: kernel_modules test_app
	sudo cp char_dev_base.ko /home/ye/nfs_shared/rootfs/lib/modules/4.1.15+ -r


kernel_modules:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

test_app:
	$(CROSS_COMPILE)gcc -o char_dev_base_app char_dev_base_app.c
	sudo cp char_dev_base_app /home/ye/nfs_shared/rootfs/lib/modules/4.1.15+ -r

clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean

test:
	echo $(CROSS_COMPILE)
	echo $(ARCH)