obj-m := led.o
KDIR := /home/q3k/linux-xlnx
PWD := $(shell pwd)

default:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) clean


.PHONY: upload

upload: default
	scp led.ko root@10.0.0.2:

run: upload
	ssh root@10.0.0.2 rmmod led || true
	ssh root@10.0.0.2 insmod led.ko
	sleep 1
	ssh root@10.0.0.2 dmesg | tail -n 100
