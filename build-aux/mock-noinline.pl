#!/usr/bin/perl

my %mockable;
my %mocked;

# Functions in public header don't get the noinline annotation
# so whitelist them here
$mockable{"virEventAddTimeout"} = 1;

foreach my $arg (@ARGV) {
    if ($arg =~ /mock\.c$/) {
        #print "Scan mock $arg\n";
        &scan_overrides($arg);
    } elsif ($arg =~ /\.c$/) {
        #print "Scan source $arg\n";
        &scan_annotations($arg);
    }
}

my $warned = 0;
foreach my $func (keys %mocked) {
    next if exists $mockable{$func};

    $warned++;
    print STDERR "$func is mocked at $mocked{$func} but missing VIR_MOCKABLE impl\n";
}

exit $warned ? 1 : 0;


sub scan_annotations {
    my $file = shift;

    open FH, $file or die "cannot read $file: $!";

    my $func;
    my $mockable = 0;
    while (<FH>) {
        if (/^VIR_MOCKABLE/) {
            $mockable = 1;
        } elsif ($mockable) {
            if (/^\s*(\w+),$/) {
                my $func = $1;
                $mockable{$func} = 1;
            }
            $mockable = 0;
        }
    }

    close FH
}

sub scan_overrides {
    my $file = shift;

    open FH, $file or die "cannot read $file: $!";

    my $func;
    while (<FH>) {
        if (/^(\w+)\(/ || /^\w+\s*(?:\*\s*)?(\w+)\(/) {
            my $name = $1;
            if ($name =~ /^vir/) {
                $mocked{$name} = "$file:$.";
            }
        }
    }

    close FH
}
