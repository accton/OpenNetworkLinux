############################################################
#
# Static mdadm and mdmon for the ONL RAID boot support.
#
# Both tools are built from one pinned upstream release and are
# linked statically. The loader initrd and the runtime system then
# use the same files, on every Debian release. The build runs in the
# ONL builder with its native toolchain; the result is placed in the
# builds directory of the architecture package that includes this
# file (packages/base/<arch>/mdadm/builds).
#
############################################################
MDADM_VERSION := 4.3
MDADM_ARCHIVE := mdadm-$(MDADM_VERSION).tar.xz
# From https://www.kernel.org/pub/linux/utils/raid/mdadm/sha256sums.asc
MDADM_SHA256 := 416727ae1f1080ea6e3090cea36dd076826fc369151e36ab736557ba92196f9f
MDADM_URL := https://www.kernel.org/pub/linux/utils/raid/mdadm/$(MDADM_ARCHIVE)
# Keep the upstream warnings, but do not stop on warnings from newer
# compilers. Build without libudev: a static libudev is usually not
# available, and only "mdadm --monitor" and waiting for udev use it.
# mdadm 4.3 still needs the libudev.h header to compile.
MDADM_CXFLAGS := -O2 -Wno-error -DNO_LIBUDEV

SBIN := sbin
LICENSE := COPYING.mdadm
WORK := .mdadm-build
SOURCE := $(WORK)/mdadm-$(MDADM_VERSION)

.PHONY: all clean

all: $(SBIN)/mdmon

# Download the source if it is not here. A copy placed here by hand
# works without network access. Both are checked before the build.
$(MDADM_ARCHIVE):
	wget -O $@.part $(MDADM_URL)
	echo "$(MDADM_SHA256)  $@.part" | sha256sum -c -
	mv $@.part $@

# The loader has no C library for these tools, so they must be static.
# mdmon is installed last: it appears only after every check passed.
$(SBIN)/mdmon: $(MDADM_ARCHIVE) $(lastword $(MAKEFILE_LIST))
	echo "$(MDADM_SHA256)  $(MDADM_ARCHIVE)" | sha256sum -c -
	rm -rf $(WORK)
	rm -f $(SBIN)/mdadm $(SBIN)/mdmon $(LICENSE)
	mkdir -p $(WORK) $(SBIN)
	tar -xf $(MDADM_ARCHIVE) -C $(WORK)
	$(MAKE) -C $(SOURCE) CC=$(CROSS_COMPILER)gcc CXFLAGS="$(MDADM_CXFLAGS)" LDFLAGS=-static RUN_DIR=/run/mdadm mdadm mdmon
	$(CROSS_COMPILER)strip $(SOURCE)/mdadm $(SOURCE)/mdmon
	for tool in mdadm mdmon; do readelf -d $(SOURCE)/$$tool | grep -q "no dynamic section" || exit 1; done
	# The two checks below run the new tools, so they need a native build.
	$(SOURCE)/mdadm --version 2>&1 | grep -q "mdadm - v$(MDADM_VERSION) "
	$(SOURCE)/mdmon --help 2>&1 | grep -q -- "--takeover"
	cp $(SOURCE)/COPYING $(LICENSE)
	install -m 0755 $(SOURCE)/mdadm $(SBIN)/mdadm
	install -m 0755 $(SOURCE)/mdmon $(SBIN)/mdmon
	rm -rf $(WORK)

clean:
	rm -rf $(WORK) $(SBIN) $(MDADM_ARCHIVE).part
	rm -f $(LICENSE)
