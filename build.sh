#!/usr/bin/env bash
set -e

BLUE='\033[0;34m'
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'

info()    { echo -e "${BLUE}[INFO]${NC} $1"; }
success() { echo -e "${GREEN}[OK]${NC} $1"; }
fail()    { echo -e "${RED}[ERROR]${NC} $1"; exit 1; }

detect_os() {
    case "$(uname -s)" in
        Darwin) echo "mac" ;;
        Linux)  echo "linux" ;;
        *)      echo "unknown" ;;
    esac
}

detect_arch() {
    uname -m
}

install_mac() {
    info "Installing macOS dependencies"

    command -v brew >/dev/null 2>&1 || \
        /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

    if [[ "$(uname -m)" == "arm64" ]]; then
        eval "$(/opt/homebrew/bin/brew shellenv)"
    fi

    brew update

    brew install \
        portaudio \
        portmidi \
        liblo \
        fftw \
        libsndfile \
        ncurses \
        pkg-config
}

install_linux() {
    info "Installing Linux dependencies"

    if command -v apt-get >/dev/null 2>&1; then
        sudo apt-get update
        sudo apt-get install -y \
            build-essential \
            pkg-config \
            portaudio19-dev \
            libportmidi-dev \
            liblo-dev \
            libfftw3-dev \
            libsndfile1-dev \
            libasound2-dev \
            libncurses-dev
    elif command -v pacman >/dev/null 2>&1; then
        sudo pacman -Syu --needed \
            base-devel \
            pkg-config \
            portaudio \
            portmidi \
            liblo \
            fftw \
            libsndfile \
            ncurses
    elif command -v yum >/dev/null 2>&1; then
        sudo yum groupinstall -y "Development Tools"
        sudo yum install -y \
            pkg-config \
            portaudio-devel \
            portmidi-devel \
            liblo-devel \
            fftw-devel \
            libsndfile-devel \
            ncurses-devel
    else
        fail "Unsupported Linux distribution"
    fi
}

verify() {
    info "Verifying pkg-config libraries"

    libs=(portaudio-2.0 fftw3 sndfile liblo)

    for lib in "${libs[@]}"; do
        pkg-config --exists "$lib" || fail "$lib not found"
    done

    [ -f /usr/include/portmidi.h ] || true

    success "Dependencies verified"
}

build() {
    info "Cleaning"
    make clean 2>/dev/null || true

    info "Building"
    make all

    [ -f SignalCrate ] || fail "Build failed"

    chmod +x SignalCrate
    success "Build complete"
}

main() {
    [ -f Makefile ] || fail "Run from Signal Crate root"

    OS=$(detect_os)
    ARCH=$(detect_arch)

    info "OS: $OS"
    info "ARCH: $ARCH"

    case "$OS" in
        mac)   install_mac ;;
        linux) install_linux ;;
        *)     fail "Unsupported OS" ;;
    esac

    verify
    build

    success "Signal Crate ready"
}

main "$@"
