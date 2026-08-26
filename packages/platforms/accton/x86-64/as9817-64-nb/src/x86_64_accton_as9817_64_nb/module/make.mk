###############################################################################
#
#
#
###############################################################################
THIS_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
ACCTON_COMMON := $(THIS_DIR)../../../../common
x86_64_accton_as9817_64_nb_INCLUDES := -I $(THIS_DIR)inc -I $(ACCTON_COMMON)/inc
x86_64_accton_as9817_64_nb_INTERNAL_INCLUDES := -I $(THIS_DIR)src -I $(ACCTON_COMMON)/inc
x86_64_accton_as9817_64_nb_DEPENDMODULE_ENTRIES := init:x86_64_accton_as9817_64_nb ucli:x86_64_accton_as9817_64_nb

# Pull in the shared accton helper library (log throttling).
include $(ACCTON_COMMON)/src/make.mk
x86_64_accton_as9817_64_nb_LIBRARIES := accton_common
