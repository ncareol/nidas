#
# Makefile for nidas Debian packages
# The primary task is to invoke scons to do the build and
# install to the $DESTDIR.
# 
# Here's a table of install directories of RPMs and Debian packages for
# various architectures. "scons install" puts things in a nidas directory
# tree, and "scons install.root" install files into system directories, under
# DESTDIR.
#
# $PREFIX is typically defined to be /opt/nidas.
#
# x86_64, amd64:
#   libs:
#     rpm (nidas-libs): $PREFIX/lib
#     deb (nidas-libs,nidas-dev): $PREFIX/lib
#   modules:
#     scons: $DESTDIR/$PREFIX/modules
#     rpm (nidas-modules):
#       should be /lib/modules/$(uname-r)/nidas
#     deb (nidas-modules-amd64): /lib/modules/$(uname-r)/nidas
#	e.g.: /lib/modules/3.16.0-4-amd64/nidas
# armhf: rpi2
#   libs:
#     scons: $DESTDIR/$PREFIX/lib
#     deb (nidas-libs,nidas-dev): $PREFIX/lib
#   modules:
#     scons: $DESTDIR/$PREFIX/modules
#     deb (nidas-modules-rpi2): 
#	/lib/modules/$(uname -r)/nidas
#	e.g.: /lib/modules/4.4.9-v7+/nidas (RPi2, May 2016)
# i386, vortex
#   libs:
#     scons: $DESTDIR/$PREFIX/lib
#     deb (nidas-libs,nidas-dev): $PREFIX/lib
#   modules:
#     scons: $DESTDIR/$PREFIX/modules
#     deb (nidas-modules-vortex): 
#	/lib/modules/$(uname -r)/nidas
#	e.g.: /lib/modules/4.15.18-vortex86dx3/nidas (vortex, Oct 2020)

SCONS = scons
BUILD ?= "host"
REPO_TAG ?= v1.2
PREFIX=/opt/nidas

# Build modules according to the default for the current target.
LINUX_MODULES := yes

.PHONY : build clean scons_install

$(info DESTDIR=$(DESTDIR))
$(info DEB_BUILD_GNU_TYPE=$(DEB_BUILD_GNU_TYPE))
$(info DEB_HOST_GNU_TYPE=$(DEB_HOST_GNU_TYPE))
$(info DEB_HOST_MULTIARCH=$(DEB_HOST_MULTIARCH))

# hack: scons --config option is passed using GCJFLAGS
$(info DEB_GCJFLAGS_MAINT_SET= $(DEB_GCJFLAGS_MAINT_SET))
$(info GCJFLAGS= $(GCJFLAGS))

build:
	$(SCONS) -C src $(GCJFLAGS) -j 4 BUILD=$(BUILD) \
		REPO_TAG=$(REPO_TAG) \
		PREFIX=$(PREFIX) \
		allow_warnings=on \
		LINUX_MODULES=$(LINUX_MODULES)

install:
	$(SCONS) -C src -j 4 BUILD=$(BUILD) \
		REPO_TAG=$(REPO_TAG) \
		INSTALL_ROOT=$(DESTDIR) \
		PREFIX=$(PREFIX) \
		LINUX_MODULES=$(LINUX_MODULES) \
		install install.root

clean:
	$(SCONS) -C src -c BUILDS="$(BUILDS)"
