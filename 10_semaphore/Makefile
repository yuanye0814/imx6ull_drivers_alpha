export ARCH := arm
export CROSS_COMPILE := /usr/local/arm/gcc-linaro-4.9.4-2017.01-x86_64_arm-linux-gnueabihf/bin/arm-linux-gnueabihf-

KERNELDIR := /home/ye/_workspace_imx6ull/linux/linux-imx
PWD := $(shell pwd)

PRJ_NAME := gpio_led_semaphore
PRJ_APP_NAME := $(PRJ_NAME)_app
NFS_DIR := /home/ye/nfs_shared/rootfs/lib/modules/4.1.15+

obj-m := $(PRJ_NAME).o

build: kernel_modules test_app
	sudo cp $(PRJ_NAME).ko $(NFS_DIR) -r
	sudo chmod 777 $(NFS_DIR)/$(PRJ_NAME).ko


kernel_modules:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

test_app:
	$(CROSS_COMPILE)gcc -o $(PRJ_APP_NAME) $(PRJ_APP_NAME).c
	sudo cp $(PRJ_APP_NAME) $(NFS_DIR) -r

clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean
	rm $(PRJ_APP_NAME)

test:
	echo $(CROSS_COMPILE)
	echo $(ARCH)