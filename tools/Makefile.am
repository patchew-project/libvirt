## Copyright (C) 2005-2016 Red Hat, Inc.
## Copyright (C) 2013 Yuto KAWAMURA(kawamuray) <kawamuray.dadada@gmail.com>
##
## This library is free software; you can redistribute it and/or
## modify it under the terms of the GNU Lesser General Public
## License as published by the Free Software Foundation; either
## version 2.1 of the License, or (at your option) any later version.
##
## This library is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
## Lesser General Public License for more details.
##
## You should have received a copy of the GNU Lesser General Public
## License along with this library.  If not, see
## <http://www.gnu.org/licenses/>.

AM_CPPFLAGS = \
	-I$(top_builddir)/include -I$(top_srcdir)/include \
	-I$(top_builddir)/gnulib/lib -I$(top_srcdir)/gnulib/lib \
	-I$(top_builddir)/src -I$(top_srcdir)/src \
	-I$(top_srcdir)/src/util \
	-I$(top_srcdir) \
	$(NULL)

# We do not want to accidentally include stuff from gnulib
# or the main src/ dir or public API dir. Specific files can
# still be included via their path relative to the root if
# needed
STANDALONE_CPPFLAGS = -I$(top_srcdir)

WARN_CFLAGS += $(STRICT_FRAME_LIMIT_CFLAGS)

AM_CFLAGS = \
	$(WARN_CFLAGS) \
	$(COVERAGE_CFLAGS) \
	$(PIE_CFLAGS) \
	$(LIBXML_CFLAGS) \
	$(NULL)

AM_LDFLAGS = \
	$(RELRO_LDFLAGS) \
	$(NO_INDIRECT_LDFLAGS) \
	$(NO_UNDEFINED_LDFLAGS) \
	$(NULL)

ICON_FILES = \
	libvirt_win_icon_16x16.ico \
	libvirt_win_icon_32x32.ico \
	libvirt_win_icon_48x48.ico \
	libvirt_win_icon_64x64.ico \
	virsh_win_icon.rc

PODFILES = \
	virt-pki-validate.pod \
	virt-sanlock-cleanup.pod \
	virt-xml-validate.pod \
	$(NULL)

MANINFILES = \

	virt-pki-validate.1.in \
	virt-sanlock-cleanup.8.in \
	virt-xml-validate.1.in \
	$(NULL)

EXTRA_DIST = \
	$(ICON_FILES) \
	$(conf_DATA) \
	virt-xml-validate.in \
	virt-pki-validate.in \
	virt-sanlock-cleanup.in \
	libvirt-guests.sysconf \
	libvirt_recover_xattrs.sh \
	$(PODFILES) \
	$(MANINFILES) \
	$(NULL)


CLEANFILES =
DISTCLEANFILES =
MAINTAINERCLEANFILES =

confdir = $(sysconfdir)/libvirt
conf_DATA =

bin_SCRIPTS = virt-xml-validate virt-pki-validate
libexec_SCRIPTS = libvirt-guests.sh
man1_MANS = \
		virt-pki-validate.1 \
		virt-xml-validate.1

if WITH_SANLOCK
sbin_SCRIPTS = virt-sanlock-cleanup
man8_MANS = virt-sanlock-cleanup.8
DISTCLEANFILES += virt-sanlock-cleanup
endif WITH_SANLOCK

virt-xml-validate: virt-xml-validate.in Makefile
	$(AM_V_GEN)sed -e 's|[@]schemadir@|$(pkgdatadir)/schemas|g' \
		       -e 's|[@]VERSION@|$(VERSION)|g' \
	  < $< > $@ || (rm $@ && exit 1) && chmod +x $@

virt-pki-validate: virt-pki-validate.in Makefile
	$(AM_V_GEN)sed -e 's|[@]sysconfdir@|$(sysconfdir)|g' \
		       -e 's|[@]VERSION@|$(VERSION)|g' \
	  < $< > $@ || (rm $@ && exit 1) && chmod +x $@

virt-sanlock-cleanup: virt-sanlock-cleanup.in Makefile
	$(AM_V_GEN)sed -e 's|[@]sysconfdir@|$(sysconfdir)|' \
	    -e 's|[@]localstatedir@|$(localstatedir)|' < $< > $@ \
	    || (rm $@ && exit 1) && chmod +x $@


BUILT_SOURCES =

if WITH_WIN_ICON
virsh_LDADD += virsh_win_icon.$(OBJEXT)

# Before you edit virsh_win_icon.rc, please note the following
# limitations of the resource file format:
#
# (1) '..' is not permitted in the icon filename field.
# (2) '-' is not permitted in the icon filename field.
# (3) Comments are not permitted in the file.
#
# Windows appears to choose the first <= 32x32 icon it finds
# in the resource file.  Therefore you should list the available
# icons from largest to smallest, and make sure that the 32x32
# icon is the most legible.
#
# Windows .ICO is a special MS-only format.  GIMP and other
# tools can write it.  However there are several variations,
# and Windows seems to do its own colour quantization.  More
# information is needed in this area.

virsh_win_icon.$(OBJEXT): virsh_win_icon.rc
	$(AM_V_GEN)$(WINDRES) \
	  --input-format rc --input $< \
	  --output-format coff --output $@
endif WITH_WIN_ICON

POD2MAN = pod2man -c "Virtualization Support" -r "$(PACKAGE)-$(VERSION)"

%.1.in: %.pod
	$(AM_V_GEN)$(POD2MAN) $< $@-t1 && \
	if grep 'POD ERROR' $@-t1; then rm $@-t1; exit 1; fi && \
	sed \
		-e 's|SYSCONFDIR|\@sysconfdir\@|g' \
		-e 's|LOCALSTATEDIR|\@localstatedir\@|g' \
	< $@-t1 > $@-t2 && \
	rm -f $@-t1 && \
	mv $@-t2 $@

%.8.in: %.pod
	$(AM_V_GEN)$(POD2MAN) --section=8 $< $@-t1 && \
	if grep 'POD ERROR' $@-t1; then rm $@-t1; exit 1; fi && \
	sed \
		-e 's|SYSCONFDIR|\@sysconfdir\@|g' \
		-e 's|LOCALSTATEDIR|\@localstatedir\@|g' \
		< $@-t1 > $@-t2 && \
	rm -f $@-t1 && \
	mv $@-t2 $@

%.1: %.1.in $(top_srcdir)/configure.ac
	$(AM_V_GEN)sed \
		-e 's|[@]sysconfdir[@]|$(sysconfdir)|g' \
		-e 's|[@]localstatedir[@]|$(localstatedir)|g' \
		< $< > $@-t && \
	mv $@-t $@

%.8: %.8.in $(top_srcdir)/configure.ac
	$(AM_V_GEN)sed \
		-e 's|[@]sysconfdir[@]|$(sysconfdir)|g' \
		-e 's|[@]localstatedir[@]|$(localstatedir)|g' \
		< $< > $@-t && \
	mv $@-t $@

install-data-local: install-systemd

uninstall-local: uninstall-systemd

install-sysconfig:
	$(MKDIR_P) $(DESTDIR)$(sysconfdir)/sysconfig
	$(INSTALL_DATA) $(srcdir)/libvirt-guests.sysconf \
	  $(DESTDIR)$(sysconfdir)/sysconfig/libvirt-guests

uninstall-sysconfig:
	rm -f $(DESTDIR)$(sysconfdir)/sysconfig/libvirt-guests
	rmdir $(DESTDIR)$(sysconfdir)/sysconfig ||:

EXTRA_DIST += libvirt-guests.sh.in

libvirt-guests.sh: libvirt-guests.sh.in $(top_builddir)/config.status
	$(AM_V_GEN)sed \
	    -e 's|[@]PACKAGE[@]|$(PACKAGE)|g' \
	    -e 's|[@]bindir[@]|$(bindir)|g' \
	    -e 's|[@]localedir[@]|$(localedir)|g' \
	    -e 's|[@]localstatedir[@]|$(localstatedir)|g' \
	    -e 's|[@]sbindir[@]|$(sbindir)|g' \
	    -e 's|[@]sysconfdir[@]|$(sysconfdir)|g' \
	    < $< > $@-t && \
	    chmod a+x $@-t && \
	    mv $@-t $@
BUILT_SOURCES += libvirt-guests.sh

EXTRA_DIST += libvirt-guests.service.in
SYSTEMD_UNIT_DIR = $(prefix)/lib/systemd/system

if LIBVIRT_INIT_SCRIPT_SYSTEMD
install-systemd: libvirt-guests.service install-sysconfig libvirt-guests.sh
	$(MKDIR_P) $(DESTDIR)$(SYSTEMD_UNIT_DIR)
	$(INSTALL_DATA) libvirt-guests.service \
	  $(DESTDIR)$(SYSTEMD_UNIT_DIR)/libvirt-guests.service

uninstall-systemd: uninstall-sysconfig
	rm -f $(DESTDIR)$(SYSTEMD_UNIT_DIR)/libvirt-guests.service
	rmdir $(DESTDIR)$(SYSTEMD_UNIT_DIR) ||:

BUILT_SOURCES += libvirt-guests.service

else ! LIBVIRT_INIT_SCRIPT_SYSTEMD
install-systemd:
uninstall-systemd:
endif ! LIBVIRT_INIT_SCRIPT_SYSTEMD

libvirt-guests.service: libvirt-guests.service.in $(top_builddir)/config.status
	$(AM_V_GEN)sed \
	    -e 's|[@]PACKAGE[@]|$(PACKAGE)|g' \
	    -e 's|[@]bindir[@]|$(bindir)|g' \
	    -e 's|[@]localedir[@]|$(localedir)|g' \
	    -e 's|[@]localstatedir[@]|$(localstatedir)|g' \
	    -e 's|[@]sbindir[@]|$(sbindir)|g' \
	    -e 's|[@]sysconfdir[@]|$(sysconfdir)|g' \
	    -e 's|[@]libexecdir[@]|$(libexecdir)|g' \
	    < $< > $@-t && \
	    mv $@-t $@



EXTRA_DIST += wireshark/util/genxdrstub.pl

if WITH_WIRESHARK_DISSECTOR

ws_plugin_LTLIBRARIES = wireshark/src/libvirt.la
wireshark_src_libvirt_la_CFLAGS = \
	-I wireshark/src $(WIRESHARK_DISSECTOR_CFLAGS) $(XDR_CFLAGS)
wireshark_src_libvirt_la_LDFLAGS = -avoid-version -module
wireshark_src_libvirt_la_SOURCES = \
	wireshark/src/packet-libvirt.h \
	wireshark/src/packet-libvirt.c \
	wireshark/src/plugin.c

wireshark/src/packet-libvirt.c: wireshark/src/packet-libvirt.h \
		wireshark/src/libvirt/protocol.h

WS_DISSECTOR_PROTO_FILES  = \
  $(abs_top_srcdir)/src/remote/remote_protocol.x \
  $(abs_top_srcdir)/src/remote/qemu_protocol.x \
  $(abs_top_srcdir)/src/remote/lxc_protocol.x \
  $(abs_top_srcdir)/src/rpc/virkeepaliveprotocol.x

wireshark/src/libvirt/protocol.h: wireshark/util/genxdrstub.pl \
		$(WS_DISSECTOR_PROTO_FILES)
	$(AM_V_GEN)$(MKDIR_P) wireshark/src/libvirt && \
	cd wireshark/src && \
	LIBVIRT_VERSION=$(LIBVIRT_VERSION) \
		  $(PERL) $(abs_top_srcdir)/tools/wireshark/util/genxdrstub.pl \
		  $(WS_DISSECTOR_PROTO_FILES)

endif WITH_WIRESHARK_DISSECTOR

clean-local:
	-rm -rf wireshark/src/libvirt

CLEANFILES += $(bin_SCRIPTS)
CLEANFILES += *.gcov .libs/*.gcda .libs/*.gcno *.gcno *.gcda *.i *.s
CLEANFILES += $(man1_MANS) $(man8_MANS)

DISTCLEANFILES += $(BUILT_SOURCES)

MAINTAINERCLEANFILES += $(MANINFILES)
