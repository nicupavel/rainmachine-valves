CONFIG_RAINMACHINE_VALVES = m


ARCH := arm
CROSS_COMPILE := /indevel/rainmachine3/aosp/prebuilts/gcc/linux-x86/arm/arm-eabi-4.6/bin/arm-eabi-
KVER := 3.2.0
KSRC := /indevel/rainmachine3/aosp/kernel
MODULE_NAME:= rainmachine-valves
MODDESTDIR := .
EXTRA_CFLAGS += -DDEV_V3


obj-${CONFIG_RAINMACHINE_VALVES}	+= rainmachine-valves.o

all: modules

modules:
	$(MAKE) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) -C $(KSRC) M=$(shell pwd)

strip:
	$(CROSS_COMPILE)strip $(MODULE_NAME).ko --strip-unneeded

install:
	install -p -m 644 $(MODULE_NAME).ko  $(MODDESTDIR)
	/sbin/depmod -a ${KVER}

uninstall:
	rm -f $(MODDESTDIR)/$(MODULE_NAME).ko
	/sbin/depmod -a ${KVER}
