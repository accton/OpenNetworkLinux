###############################################################################
#
# 
#
###############################################################################
THIS_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
ACCTON_COMMON := $(THIS_DIR)../../../../../common_lib/onlp_lib
x86_64_accton_as9737_32db_INCLUDES := -I $(THIS_DIR)inc -I $(ACCTON_COMMON)/inc
x86_64_accton_as9737_32db_INTERNAL_INCLUDES := -I $(THIS_DIR)src -I $(ACCTON_COMMON)/inc
x86_64_accton_as9737_32db_DEPENDMODULE_ENTRIES := init:x86_64_accton_as9737_32db ucli:x86_64_accton_as9737_32db

# Pull in the shared accton helper library (log throttling).
include $(ACCTON_COMMON)/src/make.mk
x86_64_accton_as9737_32db_LIBRARIES := accton_common
