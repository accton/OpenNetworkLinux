###############################################################################
#
# 
#
###############################################################################
THIS_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
ACCTON_COMMON := $(THIS_DIR)../../../../../common
x86_64_accton_as5835_54x_INCLUDES := -I $(THIS_DIR)inc -I $(ACCTON_COMMON)/inc
x86_64_accton_as5835_54x_INTERNAL_INCLUDES := -I $(THIS_DIR)src -I $(ACCTON_COMMON)/inc
x86_64_accton_as5835_54x_DEPENDMODULE_ENTRIES := init:x86_64_accton_as5835_54x ucli:x86_64_accton_as5835_54x

# Pull in the shared accton helper library (log throttling).
include $(ACCTON_COMMON)/src/make.mk
x86_64_accton_as5835_54x_LIBRARIES := accton_common
