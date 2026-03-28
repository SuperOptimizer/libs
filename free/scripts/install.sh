#!/bin/sh
# install.sh -- Build and install the free toolchain
#
# Usage:
#   ./scripts/install.sh              # installs to $HOME/.local/free
#   PREFIX=/usr/local/free ./scripts/install.sh
#   ./scripts/install.sh /opt/free    # prefix as argument

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SRC_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# Determine install prefix
if [ -n "$1" ]; then
    PREFIX="$1"
elif [ -z "$PREFIX" ]; then
    PREFIX="$HOME/.local/free"
fi

BUILD_DIR="$SRC_DIR/build"
JOBS="${JOBS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)}"

echo "=== free toolchain installer ==="
echo "Source:  $SRC_DIR"
echo "Build:   $BUILD_DIR"
echo "Prefix:  $PREFIX"
echo "Jobs:    $JOBS"
echo ""

# Build
echo "--- Building ---"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake "$SRC_DIR" -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_BUILD_TYPE=Release
cmake --build . --parallel "$JOBS"

# Install
echo ""
echo "--- Installing to $PREFIX ---"
cmake --install .

# Create convenience symlinks (cc -> free-cc, as -> free-as, etc.)
echo ""
echo "--- Creating symlinks ---"
BIN_DIR="$PREFIX/bin"
for tool in cc as ld ar nm objdump objcopy strip size strings addr2line \
            readelf dbg make cpp sortextable pahole lex ast; do
    target="free-$tool"
    link="$BIN_DIR/$tool"
    if [ -x "$BIN_DIR/$target" ]; then
        ln -sf "$target" "$link"
        echo "  $tool -> $target"
    fi
done

# Print summary
echo ""
echo "=== Installation complete ==="
echo ""
echo "Installed to: $PREFIX"
echo ""
echo "Add the toolchain to your PATH:"
echo ""
echo "  export PATH=\"$PREFIX/bin:\$PATH\""
echo ""
echo "Or add to your shell profile:"
echo ""
echo "  echo 'export PATH=\"$PREFIX/bin:\$PATH\"' >> ~/.profile"
echo ""
echo "Verify with:"
echo ""
echo "  free-cc --version"
echo ""
