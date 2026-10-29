#!/bin/sh
# Cross-build hero2ufs.sys (Storport miniport) for Windows ARM64
set -e
W=${WDK:-/home/claude/wdk/wdkx/c}
I=$W/Include/10.0.26100.0
L=$W/Lib/10.0.26100.0/km/ARM64
clang -ivfsoverlay ${WDK_OVERLAY:-/home/claude/wdk/ci-overlay.yaml} --target=aarch64-pc-windows-msvc -fms-extensions -fms-compatibility -fms-compatibility-version=19.38 \
  -nostdinc -isystem $I/km/crt -isystem $I/km -isystem $I/shared -isystem ${SDKSHARED:-/home/claude/wdk/sdkx/c/Include/10.0.26100.0/shared} \
  -D_ARM64_ -D_ARM64 -DARM64 -D_WIN64 -D_KERNEL_MODE -DNTDDI_VERSION=0x0A00000C -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 -DPOOL_NX_OPTIN=0 \
  -ffreestanding -fno-stack-protector -fno-builtin -O2 -Wall -Wno-unknown-pragmas -Wno-microsoft-anon-tag \
  -Wno-ignored-pragma-intrinsic -Wno-pragma-pack -Wno-nonportable-include-path -Wno-microsoft-enum-forward-reference -Wno-switch -Wno-missing-braces \
  -c hero2ufs.c -o hero2ufs.obj
clang --target=aarch64-pc-windows-msvc -ffreestanding -fno-stack-protector -O2 -c crtstub.c -o crtstub.obj
lld-link /nologo /driver /subsystem:native,10.00 /entry:DriverEntry /machine:arm64 /nodefaultlib \
  /release /base:0x1C0000000 /tsaware:no /stack:0x40000 /out:hero2ufs.sys hero2ufs.obj crtstub.obj \
  $L/storport.lib $L/ntoskrnl.lib $L/hal.lib
SIGN=${OSSLSIGNCODE:-/home/claude/wdk/osslsigncode/build/osslsigncode}
CERT=${CERTDIR:-/home/claude/wdk/cert}
rm -f hero2ufs.signed.sys
$SIGN sign -certs $CERT/test.crt -key $CERT/test.key -h sha256 -ph -in hero2ufs.sys -out hero2ufs.signed.sys >/dev/null && mv hero2ufs.signed.sys hero2ufs.sys
rm -f hero2ufs.obj crtstub.obj
