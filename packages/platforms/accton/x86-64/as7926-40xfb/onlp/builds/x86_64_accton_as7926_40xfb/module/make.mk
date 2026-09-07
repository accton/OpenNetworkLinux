###############################################################################
#
# 
#
###############################################################################
THIS_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
ACCTON_COMMON := $(THIS_DIR)../../../../../common_lib/onlp_lib
x86_64_accton_as7926_40xfb_INCLUDES := -I $(THIS_DIR)inc -I $(ACCTON_COMMON)/inc
x86_64_accton_as7926_40xfb_INTERNAL_INCLUDES := -I $(THIS_DIR)src -I $(ACCTON_COMMON)/inc
x86_64_accton_as7926_40xfb_DEPENDMODULE_ENTRIES := init:x86_64_accton_as7926_40xfb ucli:x86_64_accton_as7926_40xfb

# Pull in the shared accton helper library (log throttling).
include $(ACCTON_COMMON)/src/make.mk
x86_64_accton_as7926_40xfb_LIBRARIES := accton_common
