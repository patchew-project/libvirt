# -*- perl -*-
#
# Copyright (C) 2009 Red Hat, Inc.
# Copyright (C) 2018 Dan Zheng (dzheng@redhat.com)
#
# This program is free software; You can redistribute it and/or modify
# it under the GNU General Public License as published by the Free
# Software Foundation; either version 2, or (at your option) any
# later version
#
# The file "LICENSE" distributed along with this file provides full
# details of the terms and conditions
#

=pod

=head1 NAME

domain/401-ovmf-nvram.t - Test OVMF related functions and flags

=head1 DESCRIPTION

The test cases validates OVMF related APIs and flags

Sys::Virt::Domain::UNDEFINE_KEEP_NVRAM
Sys::Virt::Domain::UNDEFINE_NVRAM

=cut

use strict;
use warnings;

use Test::More tests => 8;

use Sys::Virt::TCK;
use File::stat;
use File::Copy;


my $tck = Sys::Virt::TCK->new();
my $conn = eval { $tck->setup(); };
BAIL_OUT "failed to setup test harness: $@" if $@;
END { $tck->cleanup if $tck; }


sub setup_nvram {

my $loader_path = shift;
my $nvram_template = shift;
my $nvram_path = shift;

# Install OVMF and check the two files should exist
#  - /usr/share/OVMF/OVMF_CODE.secboot.fd
#  - /usr/share/OVMF/OVMF_VARS.fd

my $ret = `yum install -y OVMF`;
diag "yum install OVMF: $ret";
my $st = stat($loader_path);
ok($st, "path $loader_path exists after OVMF is installed");
$st = stat($nvram_template);
ok($st, "path $nvram_template exists after OVMF is installed");

# Ensure the sample nvram file exists
copy($nvram_template, $nvram_path) or die "Copy failed: $!";

# Use 'q35' as machine type and add below lines to guest xml
#     <loader readonly='yes' secure='yes' type='pflash'>/usr/share/OVMF/OVMF_CODE.secboot.fd</loader>
#     <nvram template='/usr/share/OVMF/OVMF_VARS.fd'>/var/lib/libvirt/qemu/nvram/test_VARS.fd</nvram>

my $xml = $tck->generic_domain(name => "tck")->as_xml;
my $xp = XML::XPath->new($xml);
diag $xp->getNodeText("/domain/os/type/\@machine");

diag "Changing guest machine type to q35";
$xp->setNodeText("/domain/os/type/\@machine", "q35");

my $loader_node = XML::XPath::Node::Element->new('loader');
my $loader_text   = XML::XPath::Node::Text->new($loader_path);
my $attr_ro     = XML::XPath::Node::Attribute->new('readonly');
my $attr_secure = XML::XPath::Node::Attribute->new('secure');
my $attr_type = XML::XPath::Node::Attribute->new('type');

$attr_ro     -> setNodeValue('yes');
$attr_secure -> setNodeValue('yes');
$attr_type   -> setNodeValue('pflash');

$loader_node->appendChild($loader_text);
$loader_node->appendAttribute($attr_ro);
$loader_node->appendAttribute($attr_secure);
$loader_node->appendAttribute($attr_type);

my $nvram_node    = XML::XPath::Node::Element->new('nvram');
my $nvram_text    = XML::XPath::Node::Text->new($nvram_path);
my $attr_template = XML::XPath::Node::Attribute->new('template');

$attr_template     -> setNodeValue($nvram_template);

$nvram_node->appendChild($nvram_text);
$nvram_node->appendAttribute($attr_template);

my $smm_node   = XML::XPath::Node::Element->new('smm');
my $attr_state = XML::XPath::Node::Attribute->new('state');
$attr_state -> setNodeValue("on");
$smm_node -> appendAttribute($attr_state);

my ($root) = $xp->findnodes('/domain/os');
$root->appendChild($loader_node);
$root->appendChild($nvram_node);
($root) = $xp->findnodes('/domain/features');
$root->appendChild($smm_node);

$xml = $xp->findnodes_as_string('/');
diag $xml;
return $xml;
}

diag "Defining an inactive domain config with nvram";
my $loader_file_path = '/usr/share/OVMF/OVMF_CODE.secboot.fd';
my $nvram_file_template = '/usr/share/OVMF/OVMF_VARS.fd';
my $nvram_file_path = '/var/lib/libvirt/qemu/nvram/test_VARS.fd';

my $xml = setup_nvram($loader_file_path, $nvram_file_template, $nvram_file_path);
my $dom;

diag "Test Sys::Virt::Domain::UNDEFINE_KEEP_NVRAM";
ok_domain(sub { $dom = $conn->define_domain($xml) }, "defined domain with nvram configure");
diag "Checking nvram file already exists";
my $st = stat($nvram_file_path);
ok($st, "File '$nvram_file_path' exists as expected");
$dom->undefine(Sys::Virt::Domain::UNDEFINE_KEEP_NVRAM);
diag "Checking nvram file still exists";
$st = stat($nvram_file_path);
ok($st, "File '$nvram_file_path' still exists as expected");

diag "Test Sys::Virt::Domain::UNDEFINE_NVRAM";
ok_domain(sub { $dom = $conn->define_domain($xml) }, "defined domain with nvram configure");
$dom->undefine(Sys::Virt::Domain::UNDEFINE_NVRAM);
diag "Checking nvram file removed";
$st = stat($nvram_file_path);
ok(!$st, "File '$nvram_file_path' is removed");

ok_error(sub { $conn->get_domain_by_name("tck") }, "NO_DOMAIN error raised from missing domain",
	 Sys::Virt::Error::ERR_NO_DOMAIN);
