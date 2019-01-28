# -*- makefile -*-

HERE = $(shell pwd)
TOP = $(shell git rev-parse --show-toplevel)

# Default to using all possible CPUs
SMP = $(getconf _NPROCESSORS_ONLN)

# Any extra arguments to pass to make
MAKE_ARGS =

# Avoid pulling submodules over the network by locally
# cloning them
SUBMODULES = .gnulib src/keycodemapdb

IMAGE_PREFIX = quay.io/libvirt/buildenv
IMAGE_TAG = :master

# We delete the virtual root after completion, set
# to 0 if you need to keep it around for debugging
CLEAN = 1

# We'll always freshly clone the virtual root each
# time in case it was not cleaned up before. Set
# to 0 if you want to try restarting a previously
# preserved env
RECLONE = 1

SCRIPT =

# We need the container process to run with current host IDs
# so that it can access the passed in build directory
UID = $(shell id -u)
GID = $(shell id -g)

# Docker doesn't require the IDs you run as to exist in
# the container's /etc/passwd & /etc/group files, but
# if they do not, then libvirt's  'make check' will fail
# many tests
PWDB_MOUNTS = \
	--volume $(HERE)/citree/group:/etc/group:ro,z \
	--volume $(HERE)/citree/passwd:/etc/passwd:ro,z

# Docker containers can have very large ulimits
# for nofiles - as much as 1048576. This makes
# libvirt very slow at exec'ing programs.
ULIMIT_FILES = 1024

DOCKER_ARGS = \
	--rm \
	--interactive \
	--tty \
	--user $(UID):$(GID) \
	$(PWDB_MOUNTS) \
	--volume $(HERE)/citree/src:/build:z \
	--workdir /build/vpath \
        --ulimit nofile=$(ULIMIT_FILES):$(ULIMIT_FILES) \
	$(NULL)

preparetree:
	@if test "$(RECLONE)" = "1" ; then \
                rm -rf citree ; \
	fi
	@if ! test -d citree ; then \
		mkdir -p citree/src; \
		cp /etc/passwd citree; \
		cp /etc/group citree; \
		echo "Cloning $(TOP) to $(HERE)/citree/root"; \
                git clone -q --local $(TOP) $(HERE)/citree/src; \
                for mod in $(SUBMODULES) ; \
                do \
			echo "Cloning $(TOP)/$$mod to $(HERE)/citree/$$mod"; \
                        git clone -q --local $(TOP)/$$mod $(HERE)/citree/src/$$mod ; \
                done ; \
                mkdir -p citree/src/vpath ; \
	else \
		test "$(CLEAN)" = "1" && rm -rf citree || : ; \
        fi

# $LIBVIRT_CONFIGURE_OPTS is a env that can optionally be
# set in the container, populated at build time from the
# Dockerfile. A typical use case would be to pass --host/--target
# args to trigger cross-compilation
cibuild-%: preparetree
	docker run $(DOCKER_ARGS) $(IMAGE_PREFIX)-$*$(IMAGE_TAG) \
		/bin/bash -c '\
		echo "../autogen.sh $${LIBVIRT_CONFIGURE_OPTS}" && \
                ../autogen.sh $${LIBVIRT_CONFIGURE_OPTS} && make -j $(SMP) $(MAKE_ARGS)'
	@test "$(CLEAN)" = "1" && rm -rf citree || :

cicheck-%:
	$(MAKE) cibuild-$* MAKE_ARGS="check gl_public_submodule_commit="

cishell-%: preparetree
	docker run $(DOCKER_ARGS) $(IMAGE_PREFIX)-$*$(IMAGE_TAG) /bin/bash
	@test "$(CLEAN)" = "1" && rm -rf citree || :

cihelp:
	@echo "Build libvirt inside docker containers used for CI"
	@echo
	@echo "Available targets:"
	@echo
	@echo "    cibuild-\$$IMAGE  - run a default 'make'"
	@echo "    cicheck-\$$IMAGE  - run a 'make check'"
	@echo "    cishell-\$$IMAGE  - run an interactive shell"
	@echo
	@echo "Available container images:"
	@echo
	@echo "    centos-7"
	@echo "    debian-8"
	@echo "    debian-9"
	@echo "    debian-sid"
	@echo "    fedora-28"
	@echo "    fedora-29"
	@echo "    fedora-rawhide"
	@echo "    ubuntu-16"
	@echo "    ubuntu-18"
	@echo
	@echo "Available make variables:"
	@echo
	@echo "    CLEAN=0   - do not delete '$(HERE)/citree' after completion"
	@echo "    RECLONE=0 - re-use existing '$(HERE)/citree' content"
	@echo
