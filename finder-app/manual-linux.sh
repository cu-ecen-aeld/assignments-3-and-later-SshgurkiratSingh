#!/bin/bash
# Script to build a barebones kernel and rootfs for ARM

set -e
set -u

OUTDIR=/tmp/aeld
KERNEL_REPO=https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git
KERNEL_VERSION=v5.1.10
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath $(dirname $0))
ARCH=arm64
CROSS_COMPILE=aarch64-linux-gnu-

if [ $# -lt 1 ]; then
    echo "Using default directory ${OUTDIR} for output"
else
    OUTDIR=$1
    echo "Using passed directory ${OUTDIR} for output"
fi

# Check if cross-compiler is installed
if ! command -v ${CROSS_COMPILE}gcc &>/dev/null; then
    echo "Cross-compiler ${CROSS_COMPILE}gcc not found. Please install it and try again."
    exit 1
fi

mkdir -p ${OUTDIR}

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/linux-stable" ]; then
    # Clone only if the repository does not exist
    echo "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR}"
    git clone --depth 1 --single-branch --branch ${KERNEL_VERSION} ${KERNEL_REPO} linux-stable
fi

cd linux-stable
echo "Checking out version ${KERNEL_VERSION}"
git checkout ${KERNEL_VERSION}

# Apply the patch
echo "Applying dtc-lexer patch"
cat <<EOF >dtc-lexer.patch
--- a/scripts/dtc/dtc-lexer.l
+++ b/scripts/dtc/dtc-lexer.l
@@ -23,7 +23,6 @@ LINECOMMENT	"//".*\n
 #include "srcpos.h"
 #include "dtc-parser.tab.h"
 
-YYLTYPE yylloc;
 extern bool treesource_error;
 
 /* CAUTION: this will stop working if we ever use yyless() or yyunput() */
EOF

if ! patch -p1 <dtc-lexer.patch; then
    echo "Failed to apply patch. Exiting."
    exit 1
fi

if [ ! -e ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ]; then
    # Build steps
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} mrproper
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} defconfig
    make -j$(nproc) ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} all
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} dtbs
fi

echo "Adding the Image in outdir"
cp ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ${OUTDIR}/

echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]; then
    echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm -rf ${OUTDIR}/rootfs
fi

# Create necessary base directories
mkdir -p ${OUTDIR}/rootfs
cd ${OUTDIR}/rootfs
mkdir -p bin dev etc home lib lib64 proc sbin sys tmp usr var
mkdir -p usr/bin usr/lib usr/sbin
mkdir -p var/log

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/busybox" ]; then
    git clone git://busybox.net/busybox.git
    cd busybox
    git checkout ${BUSYBOX_VERSION}
    # Configure busybox
    make distclean
    make defconfig
else
    cd busybox
fi

# Make and install busybox
make distclean
make defconfig
make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE}
make CONFIG_PREFIX=${OUTDIR}/rootfs ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} install

echo "Library dependencies"
${CROSS_COMPILE}readelf -a ${OUTDIR}/rootfs/bin/busybox | grep "program interpreter"
${CROSS_COMPILE}readelf -a ${OUTDIR}/rootfs/bin/busybox | grep "Shared library"

# Add library dependencies to rootfs
SYSROOT=$(${CROSS_COMPILE}gcc -print-sysroot)

echo "SYSROOT is set to: ${SYSROOT}"
if [ ! -d "${SYSROOT}" ]; then
    echo "SYSROOT path does not exist. Exiting."
    exit 1
fi

cp -L ${SYSROOT}/lib/ld-linux-aarch64.so.1 ${OUTDIR}/rootfs/lib
cp -L ${SYSROOT}/lib64/libm.so.6 ${OUTDIR}/rootfs/lib64
cp -L ${SYSROOT}/lib64/libresolv.so.2 ${OUTDIR}/rootfs/lib64
cp -L ${SYSROOT}/lib64/libc.so.6 ${OUTDIR}/rootfs/lib64

# Make device nodes
sudo mknod -m 666 ${OUTDIR}/rootfs/dev/null c 1 3
sudo mknod -m 600 ${OUTDIR}/rootfs/dev/console c 5 1

# Clean and build the writer utility
cd ${FINDER_APP_DIR}
make clean
make CROSS_COMPILE=${CROSS_COMPILE}

# Copy the finder related scripts and executables to the /home directory
cp ${FINDER_APP_DIR}/finder.sh ${OUTDIR}/rootfs/home/
cp ${FINDER_APP_DIR}/finder-test.sh ${OUTDIR}/rootfs/home/
cp ${FINDER_APP_DIR}/writer ${OUTDIR}/rootfs/home/
cp ${FINDER_APP_DIR}/autorun-qemu.sh ${OUTDIR}/rootfs/home/
cp -r ${FINDER_APP_DIR}/conf ${OUTDIR}/rootfs/home/

# Modify finder-test.sh to use the correct path
sed -i 's|../conf/assignment.txt|conf/assignment.txt|' ${OUTDIR}/rootfs/home/finder-test.sh

# Chown the root directory
sudo chown -R root:root ${OUTDIR}/rootfs

# Create initramfs.cpio.gz
cd ${OUTDIR}/rootfs
find . | cpio -H newc -ov --owner root:root >${OUTDIR}/initramfs.cpio
gzip -f ${OUTDIR}/initramfs.cpio
