###############################################################################
#
#
#
###############################################################################
THIS_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
# module -> x86_64_accton_as9736_64d -> builds -> onlp -> as9736-64d -> x86-64 -> accton
ACCTON_COMMON := $(THIS_DIR)../../../../../common

x86_64_accton_as9736_64d_INCLUDES := -I $(THIS_DIR)inc -I $(ACCTON_COMMON)/inc
x86_64_accton_as9736_64d_INTERNAL_INCLUDES := -I $(THIS_DIR)src -I $(ACCTON_COMMON)/inc
x86_64_accton_as9736_64d_DEPENDMODULE_ENTRIES := init:x86_64_accton_as9736_64d ucli:x86_64_accton_as9736_64d

# Pull in the shared accton helper library (log throttling, ...).
include $(ACCTON_COMMON)/src/make.mk
x86_64_accton_as9736_64d_LIBRARIES := accton_common
