###############################################################################
#
# 
#
###############################################################################

LIBRARY := onlp_platform_sim
$(LIBRARY)_SUBDIR := $(dir $(lastword $(MAKEFILE_LIST)))
include $(BUILDER)/lib.mk
