## Process this file with automake to produce Makefile.in

## Copyright (C) 2005-2016 Red Hat, Inc.
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

ADMIN_EXAMPLES = \
	$(wildcard $(srcdir)/c/admin/*.c) \
	$(NULL)

DOMAIN_EXAMPLES = \
	$(wildcard $(srcdir)/c/domain/*.c) \
	$(NULL)

MISC_EXAMPLES = \
	$(wildcard $(srcdir)/c/misc/*.c) \
	$(NULL)

POLKIT_EXAMPLES = \
	$(wildcard $(srcdir)/polkit/*.rules) \
	$(NULL)

SH_EXAMPLES = \
	$(wildcard $(srcdir)/sh/*) \
	$(NULL)

STORAGE_XML_EXAMPLES = \
	$(wildcard $(srcdir)/xml/storage/*.xml) \
	$(NULL)

SYSTEMTAP_EXAMPLES = \
	$(wildcard $(srcdir)/systemtap/*.stp) \
	$(NULL)

TEST_XML_EXAMPLES = \
	$(wildcard $(srcdir)/xml/test/*.xml) \
	$(NULL)

EXTRA_DIST = \
	$(POLKIT_EXAMPLES) \
	$(SH_EXAMPLES) \
	$(STORAGE_XML_EXAMPLES) \
	$(SYSTEMTAP_EXAMPLES) \
	$(TEST_XML_EXAMPLES) \
	$(NULL)

examplesdir = $(docdir)/examples

adminexamplesdir = $(examplesdir)/c/admin
adminexamples_DATA = $(ADMIN_EXAMPLES)

domainexamplesdir = $(examplesdir)/c/domain
domainexamples_DATA = $(DOMAIN_EXAMPLES)

miscexamplesdir = $(examplesdir)/c/misc
miscexamples_DATA = $(MISC_EXAMPLES)

polkitexamplesdir = $(examplesdir)/polkit
polkitexamples_DATA = $(POLKIT_EXAMPLES)

shexamplesdir = $(examplesdir)/sh
shexamples_DATA = $(SH_EXAMPLES)

storagexmlexamplesdir = $(examplesdir)/xml/storage
storagexmlexamples_DATA = $(STORAGE_XML_EXAMPLES)

systemtapexamplesdir = $(examplesdir)/systemtap
systemtapexamples_DATA = $(SYSTEMTAP_EXAMPLES)

testxmlexamplesdir = $(examplesdir)/xml/test
testxmlexamples_DATA = $(TEST_XML_EXAMPLES)
