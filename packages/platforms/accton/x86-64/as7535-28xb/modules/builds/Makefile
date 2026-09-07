KERNELS := onl-kernel-6.1-lts-x86-64-all:amd64
KINCLUDES := $(ONL)/packages/platforms/accton/x86-64/common_lib/kernel_lib/builds/ipmi/accton_ipmi_intf.h
KMODULES := $(ONL)/packages/platforms/accton/x86-64/common_lib/kernel_lib/builds/ipmi/accton_ipmi_intf.c $(wildcard src/*.c)
VENDOR := accton
BASENAME := x86-64-accton-as7535-28xb
ARCH := x86_64
include $(ONL)/make/kmodule.mk
