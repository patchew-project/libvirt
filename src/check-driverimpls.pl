#!/usr/bin/env perl
#
# Copyright (C) 2013 Red Hat, Inc.
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library.  If not, see
# <http://www.gnu.org/licenses/>.
#

use strict;
use warnings;

my $intable = 0;
my $table;
my $mainprefix;
my %apis;

# API pairs where a driver should provide both or neither alternative.
my %pairs = (
    'domainShutdown' => 'domainShutdownFlags',
    'domainDestroy' => 'domainDestroyFlags',
    'domainSetMemory' => 'domainSetMemoryFlags',
    'domainSave' => 'domainSaveFlags',
    'domainRestore' => 'domainRestoreFlags',
    'domainSetVcpus' => 'domainSetVcpusFlags',
    'domainPinVcpu' => 'domainPinVcpuFlags',
    'domainCreate' => 'domainCreateWithFlags',
    'domainDefineXML' => 'domainDefineXMLFlags',
    'domainUndefine' => 'domainUndefineFlags',
    'domainAttachDevice' => 'domainAttachDeviceFlags',
    'domainDetachDevice' => 'domainDetachDeviceFlags',
    'domainGetSchedulerParameters' => 'domainGetSchedulerParametersFlags',
    'domainSetSchedulerParameters' => 'domainSetSchedulerParametersFlags',
    'nodeDeviceDettach' => 'nodeDeviceDetachFlags',
);

my $status = 0;
while (<>) {
    if ($intable) {
        if (/}/) {
            while (my ($old, $new) = each %pairs) {
                if (exists $apis{$old} != exists $apis{$new}) {
                    print "$ARGV:$. Inconsistent paired API '$old' vs. '$new'\n";
                    $status = 1;
                }
            }
            $intable = 0;
            $table = undef;
            $mainprefix = undef;
            %apis = ();
        } elsif (/\.(\w+)\s*=\s*(\w+),?/) {
            my $api = $1;
            my $impl = $2;

            next if $api eq "no";
            next if $api eq "name";
            next if $impl eq "NULL";

            $apis{$api} = 1;

            my $suffix = $impl;
            my $prefix = $impl;
            $prefix =~ s/^([a-z]+)(.*?)$/$1/;

            if (defined $mainprefix) {
                if ($mainprefix ne $prefix) {
                    print "$ARGV:$. Bad prefix '$prefix' for API '$api', expecting '$mainprefix'\n";
                    $status = 1;
                }
            } else {
                $mainprefix = $prefix;
            }

            if ($api !~ /^$mainprefix/) {
                $suffix =~ s/^[a-z]+//;
                $suffix =~ s/^([A-Z]+)/lc $1/e;
            }

            if ($api ne $suffix) {
                my $want = $api;
                $want =~ s/^nwf/NWF/;
                if ($api !~ /^$mainprefix/) {
                    $want =~ s/^([a-z])/uc $1/e;
                    $want = $mainprefix . $want;
                }
                print "$ARGV:$. Bad impl name '$impl' for API '$api', expecting '$want'\n";
                $status = 1;
            }
        }
    } elsif (/^(?:static\s+)?(vir(?:\w+)?Driver)\s+(?!.*;)/) {
        next if $1 eq "virNWFilterCallbackDriver" ||
                $1 eq "virNWFilterTechDriver" ||
                $1 eq "virConnectDriver";
        $intable = 1;
        $table = $1;
    }
}

exit $status;
