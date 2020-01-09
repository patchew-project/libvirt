#!/bin/sh
# Run this to generate all the initial makefiles, etc.

die()
{
    echo "error: $1" >&2
    exit 1
}

srcdir=$(dirname "$0")
test "$srcdir" || srcdir=.

cd "$srcdir" || {
    die "Failed to cd into $srcdir"
}

test -f src/libvirt.c || {
    die "$0 must live in the top-level libvirt directory"
}

no_git=
gnulib_srcdir=
while test "$#" -gt 0; do
    case "$1" in
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

# Only look into git submodules if we're in a git checkout
if test -d .git || test -f .git; then

    # Check for dirty submodules
    if test -z "$CLEAN_SUBMODULE"; then
        for path in $(git submodule status | awk '{ print $2 }'); do
            case "$(git diff "$path")" in
                *-dirty*)
                    echo "error: $path is dirty, please investigate" >&2
                    echo "set CLEAN_SUBMODULE to discard submodule changes" >&2
                    exit 1
                    ;;
            esac
        done
    fi
    if test "$CLEAN_SUBMODULE" && test -z "$no_git"; then
        echo "Cleaning up submodules..."
        git submodule foreach 'git clean -dfqx && git reset --hard' || {
            die "Cleaning up submodules failed"
        }
    fi

    # Update all submodules. If any of the submodules has not been
    # initialized yet, it will be initialized now; moreover, any submodule
    # with uncommitted changes will be returned to the expected state
    echo "Updating submodules..."
    git submodule update --init || {
        die "Updating submodules failed"
    }

    # The expected hash, eg. the one computed after the last
    # successful bootstrap run, is stored on disk
    state_file=.git-module-status
    expected_hash=$(cat "$state_file" 2>/dev/null)
    actual_hash=$(gnulib_hash "$no_git")

    if test "$actual_hash" = "$expected_hash"; then
        # The gnulib hash matches our expectations, and all the files
        # that can only be generated through bootstrap are present:
        # we just need to run autoreconf. Unless we're performing a
        # dry run, of course...
        echo "Running autoreconf..."
        autoreconf -v || {
            die "autoreconf failed"
        }
    else
        # Whenever the gnulib submodule or any of the related bits
        # has been changed in some way (see gnulib_hash) we need to
        # run bootstrap again. If we're performing a dry run, we
        # change the return code instead to signal our caller
        echo "Running bootstrap..."
        ./bootstrap$no_git || {
            die "bootstrap failed"
        }
        gnulib_hash >"$state_file"
    fi
fi
