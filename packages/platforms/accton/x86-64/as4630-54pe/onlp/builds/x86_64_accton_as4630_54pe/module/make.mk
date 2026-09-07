###############################################################################
#
# 
#
###############################################################################
THIS_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
ACCTON_COMMON := $(THIS_DIR)../../../../../common_lib/onlp_lib
x86_64_accton_as4630_54pe_INCLUDES := -I $(THIS_DIR)inc -I $(ACCTON_COMMON)/inc
x86_64_accton_as4630_54pe_INTERNAL_INCLUDES := -I $(THIS_DIR)src -I $(ACCTON_COMMON)/inc
x86_64_accton_as4630_54pe_DEPENDMODULE_ENTRIES := init:x86_64_accton_as4630_54pe ucli:x86_64_accton_as4630_54pe

# Pull in the shared accton helper library (log throttling).
include $(ACCTON_COMMON)/src/make.mk
x86_64_accton_as4630_54pe_LIBRARIES := accton_common
