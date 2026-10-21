#!/usr/bin/env bash
# Apply patches in patches/*.patch to the JUCE submodule.
# Idempotent: if a patch is already applied (verified via `git apply
# --reverse --check` succeeding), it's skipped.

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
JUCE_DIR="$REPO_ROOT/third_party/JUCE"
PATCHES_DIR="$REPO_ROOT/patches"

if [ ! -d "$JUCE_DIR" ]; then
    echo "error: JUCE submodule not found at $JUCE_DIR" >&2
    echo "       run 'git submodule update --init --recursive' first" >&2
    exit 1
fi

if [ ! -d "$PATCHES_DIR" ] || ! ls "$PATCHES_DIR"/*.patch >/dev/null 2>&1; then
    echo "no patches in $PATCHES_DIR -- nothing to do"
    exit 0
fi

# All current JUCE patches address macOS-specific compile issues
# (see patches/JUCE_PATCHES.md). On non-Darwin platforms (Linux / CI
# Windows runners), skip the entire patch loop. CI macos-latest may
# also have a different AppleClang / SDK combination than the one
# the patches were generated against; the patch loop already
# silently skips any patch that's already-applied OR does NOT apply
# cleanly via the `git apply --check` + `--reverse --check` pair.
PLATFORM="$(uname -s)"
if [ "$PLATFORM" != "Darwin" ]; then
    echo "skip JUCE patches on $PLATFORM (macOS-only)"
    exit 0
fi

cd "$JUCE_DIR"

for patch in "$PATCHES_DIR"/*.patch; do
    patch_name="$(basename "$patch")"

    # Already applied? (reverse-apply succeeds → patch is in tree)
    if git apply --reverse --check "$patch" 2>/dev/null; then
        echo "skip $patch_name (already applied)"
        continue
    fi

    # Cleanly applicable?
    if ! git apply --check "$patch" 2>/dev/null; then
        echo "error: $patch_name does not apply cleanly to JUCE submodule" >&2
        echo "       (neither already-applied nor applicable; submodule may" >&2
        echo "       have moved past the patched JUCE version — re-generate" >&2
        echo "       the patch or update patches/JUCE_PATCHES.md)" >&2
        exit 1
    fi

    git apply "$patch"
    echo "applied $patch_name"
done
