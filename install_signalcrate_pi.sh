#!/usr/bin/env bash
set -e

echo "Updating system..."
sudo apt update

echo "Installing build tools..."
sudo apt install -y \
  build-essential \
  pkg-config \
  git \
  curl

echo "Installing audio + DSP development libraries..."
sudo apt install -y \
  libportaudio2 \
  libportaudio-dev \
  libportmidi-dev \
  liblo-dev \
  libfftw3-dev \
  libsndfile1 \
  libsndfile1-dev \
  libasound2-dev \
  libncurses-dev \
  libncursesw5-dev

echo "Configuring pkg-config path for ARM..."
PKG_PATH="/usr/lib/arm-linux-gnueabihf/pkgconfig"

if ! pkg-config --variable pc_path pkg-config | grep -q "$PKG_PATH"; then
    echo "export PKG_CONFIG_PATH=$PKG_PATH:/usr/lib/pkgconfig:/usr/share/pkgconfig" >> ~/.bashrc
    export PKG_CONFIG_PATH=$PKG_PATH:/usr/lib/pkgconfig:/usr/share/pkgconfig
fi

echo "Refreshing linker cache..."
sudo ldconfig

echo "Verifying core libraries..."
echo "PortAudio:"
pkg-config --cflags --libs portaudio-2.0 || echo "PortAudio pkg-config failed"

echo "libsndfile:"
pkg-config --cflags --libs sndfile || echo "libsndfile pkg-config failed"

echo "FFTW:"
pkg-config --cflags --libs fftw3 || echo "FFTW pkg-config failed"

echo "liblo:"
pkg-config --cflags --libs liblo || echo "liblo pkg-config failed"

echo "PortMIDI (manual check — no pkg-config expected):"
ls /usr/include/portmidi.h && echo "Header OK"
ldconfig -p | grep portmidi && echo "Library OK"

echo "Done."
echo "Reopen your shell before building Signal Crate."
