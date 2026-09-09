#!/usr/bin/env bash
# Idempotent setup for the qt- learning repository.
#
# This repo is a collection of Qt/C++ learning material (a large README of tips
# plus e-books). To actually practice the examples, an agent needs a Qt6 + C++
# toolchain. This script installs that toolchain on top of Cursor's default
# image (which already provides the ubuntu user, sudo, gcc, cmake, git, curl).
#
# apt-get install is idempotent, so re-running this script is safe.
set -euo pipefail

export DEBIAN_FRONTEND=noninteractive

PACKAGES=(
  build-essential        # make + a working C/C++ linker toolchain
  cmake                  # CMake build system (used by modern Qt projects)
  libstdc++-14-dev       # libstdc++ dev files for the default clang/gcc-14 driver
  qt6-base-dev           # Qt6 Core/Gui/Widgets/Network headers + libraries
  qt6-base-dev-tools     # moc, uic, rcc, qmake6
  qt6-qpa-plugins        # QPA platform plugins incl. offscreen (headless render)
  libgl1-mesa-dev        # OpenGL headers required to link Qt6 Gui
  fonts-noto-cjk         # CJK fonts so the (Chinese) example UIs render correctly
)

sudo apt-get update -y
sudo apt-get install -y --no-install-recommends "${PACKAGES[@]}"

echo "Qt toolchain ready:"
qmake6 --version
cmake --version | head -1
