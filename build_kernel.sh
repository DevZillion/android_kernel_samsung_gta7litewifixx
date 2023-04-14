#!/bin/bash

# Written by Hakalle (Velosh) <hakalle@proton.me>.

# Clone GCC & Proton Clang.
[[ -d "$(pwd)/gcc/" ]] || git clone https://github.com/VH-Devices/toolchains -b gcc-10.3.0 gcc --depth 1 >> /dev/null 2> /dev/null
[[ -d "$(pwd)/clang/" ]] || git clone https://github.com/DevZillion/clang_r383902 -b main clang --depth 1 >> /dev/null 2> /dev/null

# Export KBUILD_BUILD_{USER,HOST} flags.
export KBUILD_BUILD_USER="zillion"
export KBUILD_BUILD_HOST="github"

# Export ARCH/SUBARCH flags.
export ARCH="arm64"
export SUBARCH="arm64"

# Export CCACHE
export CCACHE_EXEC="$(which ccache)"
export CCACHE="${CCACHE_EXEC}"
export CCACHE_COMPRESS="1"
export USE_CCACHE="1"
$CCACHE -M 50G

# Export toolchain/clang/llvm flags
export CROSS_COMPILE="$(pwd)/gcc/bin/aarch64-buildroot-linux-gnu-"
export CLANG_TRIPLE="aarch64-linux-gnu-"
export CC="$(pwd)/clang/bin/clang"

# Export if/else outdir var
export WITH_OUTDIR=true

# Clear the console
clear

# Remove out dir folder and clean the source
if [ "${WITH_OUTDIR}" == true ]; then
   if [ -d "$(pwd)/gta7litewifi" ]; then
      rm -rf gta7litewifi
   fi
fi

# Build time
if [ "${WITH_OUTDIR}" == true ]; then
   if [ ! -d "$(pwd)/gta7litewifi" ]; then
      mkdir gta7litewifi
   fi
fi

# Sync KernelSU
if [ "${BUILD_KSU}" == true ]; then
   patch -p1 < "$(pwd)/kernelsu.patch"
   curl -LSs "https://raw.githubusercontent.com/tiann/KernelSU/main/kernel/setup.sh" | bash -
else
   # Build without KernelSU :)
   # Check if exec.c.orig exists, if it exists then the KernelSU patch was previosly applied so we need to restore the original files.
   if [ -f "$(pwd)/fs/exec.c.orig" ]; then
      rm -rf "$(pwd)/KernelSU"
      rm -rf "$(pwd)/fs/exec.c"
      rm -rf "$(pwd)/fs/open.c"
      rm -rf "$(pwd)/fs/read_write.c"
      rm -rf "$(pwd)/fs/stat.c"
      mv "$(pwd)/fs/exec.c.orig" "$(pwd)/fs/exec.c"
      mv "$(pwd)/fs/open.c.orig" "$(pwd)/fs/open.c"
      mv "$(pwd)/fs/read_write.c.orig" "$(pwd)/fs/read_write.c"
      mv "$(pwd)/fs/stat.c.orig" "$(pwd)/fs/stat.c"
   fi
fi

if [ "${WITH_OUTDIR}" == true ]; then
   "${CCACHE}" make O="$(pwd)/gta7litewifi" gta7litewifi_defconfig
   "${CCACHE}" make -j`nproc` O="$(pwd)/gta7litewifi"
fi
