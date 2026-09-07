###############################################################################
#
# 
#
###############################################################################
THIS_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
ACCTON_COMMON := $(THIS_DIR)../../../../../common_lib/onlp_lib
x86_64_accton_as5916_54xks_INCLUDES := -I $(THIS_DIR)inc -I $(ACCTON_COMMON)/inc
x86_64_accton_as5916_54xks_INTERNAL_INCLUDES := -I $(THIS_DIR)src -I $(ACCTON_COMMON)/inc
x86_64_accton_as5916_54xks_DEPENDMODULE_ENTRIES := init:x86_64_accton_as5916_54xks ucli:x86_64_accton_as5916_54xks

# Pull in the shared accton helper library (log throttling).
include $(ACCTON_COMMON)/src/make.mk
x86_64_accton_as5916_54xks_LIBRARIES := accton_common
