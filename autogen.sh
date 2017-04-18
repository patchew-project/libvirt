#!/bin/sh
# Run this to generate all the initial makefiles, etc.

die()
{
    echo "error: $1" >&2
    exit 1
}

starting_point=$(pwd)

srcdir=$(dirname "$0")
test "$srcdir" || srcdir=.

cd "$srcdir" || {
    die "failed to cd into $srcdir"
}

test -f src/libvirt.c || {
    die "$0 must live in the top-level libvirt directory"
}

dry_run=
no_git=
gnulib_srcdir=
extra_args=
while test "$#" -gt 0; do
    case "$1" in
    --dry-run)
        # This variable will serve both as an indicator of the fact that a
        # dry run has been requested, and to store the result of the dry run.
        # It will be ultimately used as return code for the script, so a
        # value of 1 means "running autogen.sh is not needed at this time"
        dry_run=1
        shift
        ;;
    --no-git)
        no_git=" $1"
        shift
        ;;
    --gnulib-srcdir=*)
        gnulib_srcdir=" $1"
        shift
        ;;
    --gnulib-srcdir)
        gnulib_srcdir=" $1=$2"
        shift
        shift
        ;;
    --system)
        prefix=/usr
        sysconfdir=/etc
        localstatedir=/var
        if test -d $prefix/lib64; then
            libdir=$prefix/lib64
        else
            libdir=$prefix/lib
        fi
        extra_args="--prefix=$prefix --localstatedir=$localstatedir"
        extra_args="$extra_args --sysconfdir=$sysconfdir --libdir=$libdir"
        shift
        ;;
    *)
        # All remaining arguments will be passed to configure verbatim
        break
        ;;
    esac
done
no_git="$no_git$gnulib_srcdir"

gnulib_hash()
{
    local no_git=$1

    if test "$no_git"; then
        echo "no-git"
        return
    fi

    # Compute the hash we'll use to determine whether rerunning bootstrap
    # is required. The first is just the SHA1 that selects a gnulib snapshot.
    # The second ensures that whenever we change the set of gnulib modules used
    # by this package, we rerun bootstrap to pull in the matching set of files.
    # The third ensures that whenever we change the set of local gnulib diffs,
    # we rerun bootstrap to pull in those diffs.
    git submodule status .gnulib | awk '{ print $1 }'
    git hash-object bootstrap.conf
    git ls-tree -d HEAD gnulib/local | awk '{ print $3 }'
}

gnulib_update_required()
{
    local expected=$1
    local actual=$2
    local no_git=$3

    local ret=0

    # Whenever the gnulib submodule or any of the related bits has been
    # changed in some way (see gnulib_hash) we need to update the submodule,
    # eg. run bootstrap again; updating is also needed if any of the files
    # that can only be generated through bootstrap has gone missing
    if test "$actual" = "$expected" && \
       test -f po/Makevars && test -f AUTHORS; then
        ret=1
    fi

    return "$ret"
}

gnulib_update()
{
    local expected=$1
    local actual=$2
    local no_git=$3

    local ret=0

    # Depending on whether or not an update is required, we might be able to
    # get away with simply running autoreconf, or we might have to go through
    # the bootstrap process
    if gnulib_update_required "$expected" "$actual" "$no_git"; then
        echo "Running bootstrap..."
        ./bootstrap$no_git --bootstrap-sync
        ret=$?
    else
        echo "Running autoreconf..."
        autoreconf -if
        ret=$?
    fi

    return "$ret"
}

if test -d .git || test -f .git; then

    if test -z "$CLEAN_SUBMODULE"; then
        git submodule status | while read _ path _; do
            dirty=$(git diff "$path")
            case "$dirty" in
                *-dirty*)
                    echo "error: $path is dirty, please investigate" >&2
                    echo "set CLEAN_SUBMODULE to discard submodule changes" >&2
                    exit 1
                    ;;
            esac
        done
    fi
    if test "$CLEAN_SUBMODULE" && test -z "$no_git"; then
        if test -z "$dry_run"; then
            echo "Cleaning up submodules..."
            git submodule foreach 'git clean -dfqx && git reset --hard' || {
                die "cleaning up submodules failed"
            }
        fi
    fi

    curr_status=.git-module-status
    expected=$(cat "$curr_status" 2>/dev/null)
    actual=$(gnulib_hash "$no_git")

    if test "$dry_run"; then
        if gnulib_update_required "$expected" "$actual" "$no_git"; then
            dry_run=0
        fi
    else
        gnulib_update "$expected" "$actual" "$no_git" || {
            die "submodule update failed"
        }
        gnulib_hash >"$curr_status"
    fi
fi

# When performing a dry run, we can stop here
test "$dry_run" && exit "$dry_run"

# If asked not to run configure, we can stop here
test "$NOCONFIGURE" && exit 0

cd "$starting_point" || {
    die "failed to cd into $starting_point"
}

if test "$OBJ_DIR"; then
    mkdir -p "$OBJ_DIR" || {
        die "failed to create $OBJ_DIR"
    }
    cd "$OBJ_DIR" || {
        die "failed to cd into $OBJ_DIR"
    }
fi

if test -z "$*" && test -z "$extra_args" && test -f config.status; then
    ./config.status --recheck || {
        die "config.status failed"
    }
else
    if test -z "$*" && test -z "$extra_args"; then
        echo "I am going to run ./configure with no arguments - if you wish"
        echo "to pass any to it, please specify them on the $0 command line."
    else
        echo "Running ./configure with $extra_args $@"
    fi
    $srcdir/configure $extra_args "$@" || {
        die "configure failed"
    }
fi

echo
echo "Now type 'make' to compile libvirt."
