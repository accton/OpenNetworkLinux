###############################################################################
#
#
#
###############################################################################
THIS_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
# module -> x86_64_accton_as5915_18x -> builds -> onlp -> as5915-18x -> x86-64 -> accton
ACCTON_COMMON := $(THIS_DIR)../../../../../common_lib/onlp_lib

x86_64_accton_as5915_18x_INCLUDES := -I $(THIS_DIR)inc -I $(ACCTON_COMMON)/inc
x86_64_accton_as5915_18x_INTERNAL_INCLUDES := -I $(THIS_DIR)src -I $(ACCTON_COMMON)/inc
x86_64_accton_as5915_18x_DEPENDMODULE_ENTRIES := init:x86_64_accton_as5915_18x ucli:x86_64_accton_as5915_18x

# Pull in the shared accton helper library (log throttling, ...).
include $(ACCTON_COMMON)/src/make.mk
x86_64_accton_as5915_18x_LIBRARIES := accton_common
