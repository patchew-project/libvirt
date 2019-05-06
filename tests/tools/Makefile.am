# vim: filetype=automake

AM_CPPFLAGS = \
	-I$(top_srcdir)/tests/ \
	-I$(top_builddir) -I$(top_srcdir) \
	-I$(top_builddir)/gnulib/lib -I$(top_srcdir)/gnulib/lib \
	-I$(top_builddir)/include -I$(top_srcdir)/include \
	-I$(top_builddir)/src -I$(top_srcdir)/src \
	-I$(top_srcdir)/src/util \
	-I$(top_srcdir)/src/conf \
	$(NULL)

WARN_CFLAGS += $(RELAXED_FRAME_LIMIT_CFLAGS)

AM_CFLAGS = \
	-Dabs_builddir="\"$(abs_builddir)\"" \
	-Dabs_top_builddir="\"$(abs_top_builddir)\"" \
	-Dabs_srcdir="\"$(abs_srcdir)\"" \
	-Dabs_top_srcdir="\"$(abs_top_srcdir)\"" \
	$(LIBXML_CFLAGS) \
	$(LIBNL_CFLAGS) \
	$(GNUTLS_CFLAGS) \
	$(SASL_CFLAGS) \
	$(SELINUX_CFLAGS) \
	$(APPARMOR_CFLAGS) \
	$(YAJL_CFLAGS) \
	$(XDR_CFLAGS) \
	$(WARN_CFLAGS)

AM_LDFLAGS = \
	-export-dynamic

MOCKLIBS_LDFLAGS = -module -avoid-version \
	-rpath /evil/libtool/hack/to/force/shared/lib/creation \
	$(MINGW_EXTRA_LDFLAGS)

GNULIB_LIBS = \
	../../gnulib/lib/libgnu.la

MOCKLIBS_LIBS = \
	$(GNULIB_LIBS) \
	../../src/libvirt.la

PROBES_O =
if WITH_DTRACE_PROBES
PROBES_O += ../../src/libvirt_probes.lo
endif WITH_DTRACE_PROBES

LDADDS = \
	$(NO_INDIRECT_LDFLAGS) \
	$(PROBES_O) \
	$(GNULIB_LIBS) \
	../../src/libvirt.la

test_helpers =
test_libraries =

if WITH_QEMU
test_helpers += qemucapsprobe
test_libraries += qemucapsprobemock.la

qemucapsprobe_SOURCES = \
	qemucapsprobe.c \
	../testutils.h
qemucapsprobe_LDADD = \
	../libqemutestdriver.la $(LDADDS)

qemucapsprobemock_la_SOURCES = \
	qemucapsprobemock.c
qemucapsprobemock_la_LDFLAGS = $(MOCKLIBS_LDFLAGS)
qemucapsprobemock_la_LIBADD = $(MOCKLIBS_LIBS)
endif WITH_QEMU


if WITH_TESTS
noinst_PROGRAMS = $(test_helpers)
noinst_LTLIBRARIES = $(test_libraries)
else ! WITH_TESTS
check_PROGRAMS = $(test_helpers)
check_LTLIBRARIES = $(test_libraries)
endif ! WITH_TESTS

EXTRA_DIST = \
	.valgrind.supp \
	oomtrace.pl
