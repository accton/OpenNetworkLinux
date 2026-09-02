###############################################################################
#
# Common library shared by Accton platform ONLP modules.
#
###############################################################################

LIBRARY := accton_common
$(LIBRARY)_SUBDIR := $(dir $(lastword $(MAKEFILE_LIST)))
include $(BUILDER)/lib.mk
