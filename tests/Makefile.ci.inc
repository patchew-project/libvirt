# -*- makefile -*-

HERE = $(shell pwd)
TOP = $(shell git rev-parse --show-toplevel)

# Run in a separate build directory. Set to empty
# for a in-source tree build, but note SRCDIR must
# also be set to a corresponding relative path
VPATHDIR = vpath
SRCDIR = ..

# Can be overridden with mingw{32,64}-configure if desired
CONFIGURE = $(SRCDIR)/configure

# Default to using all possible CPUs
SMP = $(shell getconf _NPROCESSORS_ONLN)

# Any extra arguments to pass to make
MAKE_ARGS =

# Any extra arguments to pass to configure
CONFIGURE_ARGS =

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

# Args to use when cloning a git repo
GIT_ARGS = \
	-c advice.detachedHead=false \
	-q \
	--local  \
	$(NULL)

# Args to use when running the docker env
#   --rm      stop inactive containers getting left behind
#   --user    we execute as the same user & group account
#             as dev so that file ownership matches host
#             instead of root:root
#   --volume  to pass in the cloned git repo & config
#   --workdir to set cwd to vpath build location
#   --ulimit  lower files limit for performance reasons
#   --interactive
#   --tty     Ensure we have ability to Ctrl-C the build
DOCKER_ARGS = \
	--rm \
	--user $(UID):$(GID) \
	--interactive \
	--tty \
	$(PWDB_MOUNTS) \
	--volume $(HERE)/citree/src:/build:z \
	--workdir /build/$(VPATHDIR) \
	--ulimit nofile=$(ULIMIT_FILES):$(ULIMIT_FILES) \
	$(NULL)

checkdocker:
	@echo -n "Checking if docker is available and running..." && \
	docker version 1>/dev/null && echo "yes"

preparetree:
	@if test "$(RECLONE)" = "1" ; then \
		rm -rf citree ; \
	fi
	@if ! test -d citree ; then \
		mkdir -p citree/src; \
		cp /etc/passwd citree; \
		cp /etc/group citree; \
		echo "Cloning $(TOP) to $(HERE)/citree/root"; \
		git clone $(GIT_ARGS) $(TOP) $(HERE)/citree/src || exit 1; \
		for mod in $(SUBMODULES) ; \
		do \
			if test -d $(TOP)/$$mod ; \
			then \
				echo "Cloning $(TOP)/$$mod to $(HERE)/citree/$$mod"; \
				git clone $(GIT_ARGS) $(TOP)/$$mod $(HERE)/citree/src/$$mod || exit 1; \
			fi ; \
		done ; \
		mkdir -p citree/src/$(VPATHDIR) ; \
	else \
		test "$(CLEAN)" = "1" && rm -rf citree || : ; \
	fi

# $CONFIGURE_OPTS is a env that can optionally be set in the container,
# populated at build time from the Dockerfile. A typical use case would
# be to pass --host/--target args to trigger cross-compilation
#
# This can be augmented by make local args in $(CONFIGURE_ARGS)
cibuild-%: checkdocker preparetree
	docker run $(DOCKER_ARGS) $(IMAGE_PREFIX)-$*$(IMAGE_TAG) \
		/bin/bash -c '\
		NOCONFIGURE=1 $(SRCDIR)/autogen.sh || exit 1 ; \
		$(CONFIGURE) $${CONFIGURE_OPTS}; $(CONFIGURE_ARGS) \
		if test $$? != 0 ; \
		then \
			test -f config.log && cat config.log ; \
			exit 1 ; \
		fi; \
		find -name test-suite.log -delete ; \
		make -j $(SMP) $(MAKE_ARGS) ; \
		if test $$? != 0 ; then \
			LOGS=`find -name test-suite.log` ; \
			if test "$${LOGS}" != "" ; then \
				echo "=== LOG FILE(S) START ===" ; \
				cat $${LOGS} ; \
				echo "=== LOG FILE(S) END ===" ; \
			fi ; \
			exit 1 ;\
		fi'
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
	@echo "Available x86 container images:"
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
