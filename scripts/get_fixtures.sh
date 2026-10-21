#!/bin/sh
# get_fixtures.sh [--from-file ZIP] [--test] [DIR]
#
# Downloads the MATLAB reference data the tests compare against (Iceberg MATLAB v1.0.0
# renders, published on Zenodo), checks its SHA-256 and unpacks it into DIR (default:
# ../iceberg-cpp-fixtures next to this repository, where the tests look). --from-file
# uses a ZIP already on disk instead of downloading. --test then builds the library and
# runs the tests against the data.

set -eu

ZENODO_RECORD="23543831"   # doi:10.5281/zenodo.23543831
ZIP_NAME="iceberg-cpp-fixtures-matlab-1.0.0.zip"
SHA256="33a6c5d5f0e63f8c3e9958c25424ac7ebf06a0c77e17a7321a45c8060bf4684d"

FROM=""
RUN_TESTS=0
while [ $# -gt 0 ]; do
    case "$1" in
        --from-file) FROM="$2"; shift 2 ;;
        --test) RUN_TESTS=1; shift ;;
        -h|--help) sed -n 2,8p "$0"; exit 0 ;;
        *) break ;;
    esac
done
ROOT=$(cd "$(dirname "$0")/.." && pwd)
DIR=${1:-$ROOT/../iceberg-cpp-fixtures}

if [ -f "$DIR/manifest.json" ]; then
    echo "Reference data already in $DIR."
else
    if [ -e "$DIR" ] && [ -n "$(ls -A "$DIR" 2>/dev/null)" ]; then
        echo "$DIR exists and is not empty; give another directory." >&2
        exit 1
    fi
    TMP=$(mktemp -d)
    trap 'rm -rf "$TMP"' EXIT
    if [ -n "$FROM" ]; then
        cp "$FROM" "$TMP/$ZIP_NAME"
    else
        echo "Downloading $ZIP_NAME (about 280 MB) from Zenodo record $ZENODO_RECORD..."
        # Retried with a pause: a busy server may refuse a download for a while.
        curl -fL --retry 5 --retry-delay 20 --retry-all-errors --progress-bar -o "$TMP/$ZIP_NAME" \
            "https://zenodo.org/records/$ZENODO_RECORD/files/$ZIP_NAME?download=1"
    fi
    if command -v sha256sum >/dev/null 2>&1; then
        GOT=$(sha256sum "$TMP/$ZIP_NAME" | cut -d' ' -f1)
    else
        GOT=$(shasum -a 256 "$TMP/$ZIP_NAME" | cut -d' ' -f1)
    fi
    if [ "$GOT" != "$SHA256" ]; then
        echo "Checksum mismatch: expected $SHA256, got $GOT." >&2
        exit 1
    fi
    mkdir -p "$DIR"
    unzip -q "$TMP/$ZIP_NAME" -d "$DIR"
    echo "Reference data unpacked into $DIR."
fi

if [ "$RUN_TESTS" = 1 ]; then
    BUILD="$ROOT/build-fixtures"
    cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DICEBERG_FIXTURES_DIR="$DIR"
    cmake --build "$BUILD" -j
    ctest --test-dir "$BUILD" --output-on-failure
fi
