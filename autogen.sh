#!/bin/sh
# Run this to generate all the initial makefiles, etc.

die()
{
    echo "error: $1" >&2
    exit 1
}

git_submodules()
{
    # Print the list of known submodules. Parse the .gitmodules file because
    # we want the submodule's alias rather than its path (as we're going to
    # use it for building eg. function names) and the 'git submodule' command
    # doesn't provide us with that information
    grep -E '^\[submodule ' .gitmodules | while read _ name; do
        name=${name#\"}
        name=${name%%\"*}
        echo "$name"
    done
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

    for submodule in $(git_submodules); do
        functions_file=".submodules/$submodule.functions"
        status_file=".submodules/$submodule.status"

        # No need to check for the file's existence: the script will
        # abort if it's not present
        . "$functions_file"

        # hash_function(no_git):
        # @no_git: whether to avoid using git for updates
        #
        # This function must print a hash encompassing the entire submodule
        # status. The hash will be remembered between runs and will be used
        # to determine whether a submodule update is required.
        #
        # Output: submodule hash
        # Return value: ignored
        hash_function="${submodule}_hash"
        type "$hash_function" >/dev/null 2>&1 || {
            die "required function $hash_function missing"
        }

        # update_required_function(expected, actual, no_git):
        # @expected: expected hash (from previous run)
        # @actual: actual hash (from current run)
        # @no_git: whether to avoid using git for updates
        #
        # This function is used to determine whether a submodule update is
        # required. It must perform no action, regardless of the outcome:
        # it's mostly intended for use with --dry-run.
        #
        # Output: ignored
        # Return value: 0 if update is required, 1 otherwise
        update_required_function="${submodule}_update_required"
        type "$update_required_function" >/dev/null 2>&1 || {
            die "required function $update_required_function missing"
        }

        # update_function(expected, actual, no_git):
        # @expected: expected hash (from previous run)
        # @actual: actual hash (from current run)
        # @no_git: whether to avoid using git for updates
        #
        # This function must perform the submodule update; if no update
        # is required, it can decide to skip it, but it will be called
        # regardless.
        #
        # Output: ignored
        # Return value: 0 if update was successful, 1 otherwise
        update_function="${submodule}_update"
        type "$update_function" >/dev/null 2>&1 || {
            die "required function $update_function missing"
        }

        expected=$(cat "$status_file" 2>/dev/null)
        actual=$("$hash_function" "$no_git")

        if test "$dry_run"; then
            if "$update_required_function" "$expected" "$actual" "$no_git"; then
                dry_run=0
            fi
        else
            "$update_function" "$expected" "$actual" "$no_git" || {
                die "submodule update failed"
            }
            "$hash_function" >"$status_file"
        fi
    done
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
