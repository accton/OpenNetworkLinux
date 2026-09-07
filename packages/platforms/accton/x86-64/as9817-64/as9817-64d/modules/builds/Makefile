KERNELS := onl-kernel-6.12-lts-x86-64-all:amd64
KINCLUDES := $(ONL)/packages/platforms/accton/x86-64/common_lib/kernel_lib/builds/ipmi/accton_ipmi_intf.h
KMODULES := $(ONL)/packages/platforms/accton/x86-64/common_lib/kernel_lib/builds/ipmi/accton_ipmi_intf.c $(wildcard $(ONL)/packages/platforms/accton/x86-64/as9817-64/src/modules/*.c)
VENDOR := accton
BASENAME := x86-64-accton-as9817-64d
ARCH := x86_64
include $(ONL)/make/kmodule.mk
