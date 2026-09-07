###############################################################################
#
#
#
###############################################################################
THIS_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
# module -> x86_64_accton_as9817_64 -> src -> as9817-64 -> x86-64 -> accton
ACCTON_COMMON := $(THIS_DIR)../../../../common_lib/onlp_lib

x86_64_accton_as9817_64_INCLUDES := -I $(THIS_DIR)inc -I $(ACCTON_COMMON)/inc
x86_64_accton_as9817_64_INTERNAL_INCLUDES := -I $(THIS_DIR)src -I $(ACCTON_COMMON)/inc
x86_64_accton_as9817_64_DEPENDMODULE_ENTRIES := init:x86_64_accton_as9817_64 ucli:x86_64_accton_as9817_64

# Pull in the shared accton helper library (log throttling, ...).
include $(ACCTON_COMMON)/src/make.mk
x86_64_accton_as9817_64_LIBRARIES := accton_common
