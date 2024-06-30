#!/bin/bash

set -e # Exit script on any error
set -u # Treat unset variables as errors
set -x # Print each executed command for debugging

# Define output directory and versions
OUTDIR=/tmp/aeld
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.1.10
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath $(dirname $0))

ARCH=arm64
CROSS_COMPILE=aarch64-none-linux-gnu-

# Check if output directory is provided as argument
if [ $# -ge 1 ]; then
    OUTDIR=$1
fi

# Create output directory if it doesn't exist
mkdir -p ${OUTDIR}

# Change to output directory
cd "$OUTDIR"

# Verify GCC path and availability
GCC_PATH=$(which aarch64-none-linux-gnu-gcc)
if [ -z "$GCC_PATH" ]; then
    echo "Cross-compiler not found in PATH. Please ensure it's installed and in your PATH."
    exit 1
fi
CROSS_COMPILE=$(dirname "$GCC_PATH")/aarch64-none-linux-gnu-
${CROSS_COMPILE}gcc --version
# Clone Linux kernel repository if not already cloned
if [ ! -d "${OUTDIR}/linux-stable" ]; then
    echo "Cloning Linux kernel repository"
    git clone ${KERNEL_REPO} --depth 1 --single-branch --branch ${KERNEL_VERSION} linux-stable
fi

cd ${OUTDIR}

# Build Linux kernel if necessary
if [ ! -e ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ]; then
    echo "Building Linux kernel"
    cd linux-stable
    git checkout ${KERNEL_VERSION}
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} mrproper
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} defconfig
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} -j$(nproc)
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} modules
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} dtbs
    cd ..
fi

# Copy kernel Image to output directory
cp "${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image" "${OUTDIR}/"

# Create root filesystem staging directory
echo "Creating root filesystem staging directory"
if [ -d "${OUTDIR}/rootfs" ]; then
    sudo rm -rf ${OUTDIR}/rootfs
fi
sudo mkdir -p ${OUTDIR}/rootfs/{bin,dev,etc,home,lib,lib64,proc,sbin,sys,tmp,usr,var}

# Set ownership of root filesystem to current user
sudo chown -R "$(whoami):$(id -gn)" "${OUTDIR}/rootfs"

# Clone BusyBox if not already cloned
if [ ! -d "${OUTDIR}/busybox" ]; then
    echo "Cloning BusyBox repository"
    git clone git://busybox.net/busybox.git
    cd busybox
    git checkout ${BUSYBOX_VERSION}
    cd ..
fi

# Build and install BusyBox
echo "Building BusyBox"
cd busybox
make distclean
make defconfig
make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE}
make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} CONFIG_PREFIX=${OUTDIR}/rootfs install
cd ..

# Prepare necessary library dependencies in root filesystem
echo "Preparing library dependencies"
SYSROOT=$(${CROSS_COMPILE}gcc -print-sysroot)
INTERPRETER=$(find $SYSROOT -name "ld-linux-aarch64.so.1")
cp ${INTERPRETER} ${OUTDIR}/rootfs/lib
cp ${INTERPRETER} ${OUTDIR}/rootfs/home
SHARED_LIBS=$(find $SYSROOT -name "libm.so.6" -o -name "libresolv.so.2" -o -name "libc.so.6")
for lib in ${SHARED_LIBS}; do
    cp ${lib} ${OUTDIR}/rootfs/lib64
    cp ${lib} ${OUTDIR}/rootfs/home
done

# Create necessary device nodes
echo "Creating device nodes"
sudo mknod -m 666 ${OUTDIR}/rootfs/dev/null c 1 3
sudo mknod -m 600 ${OUTDIR}/rootfs/dev/console c 5 1

# Build application-specific utilities (assuming 'writer' is one of them)
echo "Building application-specific utilities"
cd ${FINDER_APP_DIR}
make clean
make CROSS_COMPILE=${CROSS_COMPILE}

# Copy application-related scripts and executables to root filesystem
echo "Copying application-related files to root filesystem"
cd ${FINDER_APP_DIR}/conf
sudo cp -RH * ${OUTDIR}/rootfs/home

cp ${FINDER_APP_DIR}/finder.sh ${OUTDIR}/rootfs/home/
cp ${FINDER_APP_DIR}/finder-test.sh ${OUTDIR}/rootfs/home/
cp ${FINDER_APP_DIR}/writer ${OUTDIR}/rootfs/home/
cp ${FINDER_APP_DIR}/autorun-qemu.sh ${OUTDIR}/rootfs/home/
cp -r ${FINDER_APP_DIR}/conf ${OUTDIR}/rootfs/home/
sudo chown -R root:root ${OUTDIR}/rootfs

# Create initramfs.cpio.gz
echo "Creating initramfs"
cd ${OUTDIR}/rootfs
find . | cpio -H newc -ov --owner root:root >${OUTDIR}/initramfs.cpio
gzip -f ${OUTDIR}/initramfs.cpio

echo "Build completed successfully"
