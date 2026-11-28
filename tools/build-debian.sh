#!/usr/bin/env bash
# Swiftify, a spotify player for desktop
# Copyright (C) 2026  shred0s
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

# build-debian.sh - Build and install swiftify on Debian/Ubuntu
#
# Requires Qt 6.12.1 or newer. Set CMAKE_PREFIX_PATH to select a Qt kit.
# Run from the repository root or pass the repo path as an argument.
#
# Usage:
#   ./tools/build-debian.sh
#   ./tools/build-debian.sh --install-deps      # also install build dependencies
#   ./tools/build-debian.sh --prefix ~/.local   # custom install prefix

set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

log()   { echo -e "${GREEN}[*]${NC} $1"; }
warn()  { echo -e "${YELLOW}[!]${NC} $1"; }
error() { echo -e "${RED}[x]${NC} $1" >&2; exit 1; }

INSTALL_DEPS=false
PREFIX="$HOME/.local"
REPO_DIR=""

while [[ $# -gt 0 ]]; do
    case $1 in
        --install-deps) INSTALL_DEPS=true; shift ;;
        --prefix) PREFIX="$2"; shift 2 ;;
        --help|-h)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --install-deps    Install build dependencies (requires sudo)"
            echo "  --prefix PATH     Installation prefix (default: ~/.local)"
            echo "  -h, --help        Show this help"
            exit 0
            ;;
        *) REPO_DIR="$1"; shift ;;
    esac
done

# Find repo root
if [[ -n "$REPO_DIR" ]]; then
    cd "$REPO_DIR"
elif [[ -f "CMakeLists.txt" ]] && grep -q "project(Swiftify" CMakeLists.txt 2>/dev/null; then
    : # already in repo root
elif [[ -f "../CMakeLists.txt" ]] && grep -q "project(Swiftify" ../CMakeLists.txt 2>/dev/null; then
    cd ..
else
    error "Run this script from the swiftify repository root, or pass the path as an argument"
fi

REPO_DIR="$(pwd)"
log "Building swiftify in $REPO_DIR"

# Check for required tools
command -v cmake >/dev/null 2>&1 || error "cmake not found. Install it or run with --install-deps"
command -v ninja >/dev/null 2>&1 || error "ninja not found. Install it or run with --install-deps"
command -v rustc >/dev/null 2>&1 || error "rustc not found. Install Rust from https://rustup.rs"

# Install dependencies if requested
if $INSTALL_DEPS; then
    log "Installing build dependencies..."
    sudo apt-get update
    sudo apt-get install -y \
        git qt6-base-dev qt6-declarative-dev qt6-shader-baker qt6-shadertools-dev \
        qt6-wayland qt6-websockets-dev qt6-networkauth-dev qtkeychain-qt6-dev qcoro-qt6-dev libpulse-dev libasound2-dev \
        libssl-dev cmake ninja-build pkg-config
fi

# Configure
log "Configuring build..."
cmake -S . -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=OFF \
    -DCMAKE_INSTALL_PREFIX="$PREFIX"

# Build
log "Building (this may take a few minutes)..."
cmake --build build

# Install
log "Installing to $PREFIX..."
cmake --install build

# Check PATH
if [[ ":$PATH:" != *":$PREFIX/bin:"* ]]; then
    warn "$PREFIX/bin is not in your PATH"
    echo ""
    echo "Add this to your ~/.bashrc or ~/.zshrc:"
    echo "  export PATH=\"$PREFIX/bin:\$PATH\""
    echo ""
fi

log "Done! Run 'swiftify' to start the app."
echo ""
echo "Before first use:"
echo "  1. You need Spotify Premium"
echo "  2. Create a Spotify app at https://developer.spotify.com/dashboard"
echo "  3. Add http://127.0.0.1:8898/login as a redirect URI"
echo "  4. Copy your Client ID (the app will ask for it)"
