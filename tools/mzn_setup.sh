#!/bin/sh
# POSIX shell script to download and setup MiniZinc 2.10.1

set -e

CACHE_DIR="$HOME/.cache/psolve-mzn"
ARCHIVE="MiniZinc-2.10.1-x86_64-linux-gnu.tar.gz"
EXPECTED_SHA256="ef7b97d7ce9bf848d2f7b5e412d4affb8f52167cb6fd0540c7c0a81a02940d1b"
URL="https://github.com/MiniZinc/libminizinc/releases/download/2.10.1/${ARCHIVE}"
INSTALL_DIR="$CACHE_DIR/MiniZinc-2.10.1-x86_64-linux-gnu"
MINIZINC_BIN="$INSTALL_DIR/bin/minizinc"

# Check if already installed
if [ -x "$MINIZINC_BIN" ]; then
    echo "$MINIZINC_BIN"
    exit 0
fi

mkdir -p "$CACHE_DIR"

# Download if archive doesn't exist or checksum doesn't match
ARCHIVE_PATH="$CACHE_DIR/$ARCHIVE"
NEED_DOWNLOAD=1
if [ -f "$ARCHIVE_PATH" ]; then
    # Verify checksum
    if command -v sha256sum >/dev/null 2>&1; then
        ACTUAL=$(sha256sum "$ARCHIVE_PATH" | awk '{print $1}')
    elif command -v shasum >/dev/null 2>&1; then
        ACTUAL=$(shasum -a 256 "$ARCHIVE_PATH" | awk '{print $1}')
    else
        ACTUAL=""
    fi
    if [ "$ACTUAL" = "$EXPECTED_SHA256" ]; then
        NEED_DOWNLOAD=0
    fi
fi

if [ $NEED_DOWNLOAD -eq 1 ]; then
    if command -v curl >/dev/null 2>&1; then
        curl -L -o "$ARCHIVE_PATH" "$URL"
    elif command -v wget >/dev/null 2>&1; then
        wget -O "$ARCHIVE_PATH" "$URL"
    else
        echo "Error: curl or wget required" >&2
        exit 1
    fi
    
    # Verify checksum
    if command -v sha256sum >/dev/null 2>&1; then
        ACTUAL=$(sha256sum "$ARCHIVE_PATH" | awk '{print $1}')
    elif command -v shasum >/dev/null 2>&1; then
        ACTUAL=$(shasum -a 256 "$ARCHIVE_PATH" | awk '{print $1}')
    else
        echo "Warning: could not verify checksum" >&2
        ACTUAL="$EXPECTED_SHA256"
    fi
    
    if [ "$ACTUAL" != "$EXPECTED_SHA256" ]; then
        echo "Error: checksum mismatch" >&2
        exit 1
    fi
fi

# Extract
cd "$CACHE_DIR"
tar -xzf "$ARCHIVE"

if [ -x "$MINIZINC_BIN" ]; then
    echo "$MINIZINC_BIN"
    exit 0
else
    echo "Error: extraction failed" >&2
    exit 1
fi
