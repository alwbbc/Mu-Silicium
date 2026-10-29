#!/system/bin/sh
# hero2lte: put Windows 11 ARM64 on the Galaxy S7 Edge from TWRP.
# Replaces USERDATA (all Android data is lost) with ESP (300 MB) + WINDOWS.
#   sh /tmp/h2/h2install.sh info|partition|format|apply|done
D=/tmp/h2
DISK=/dev/block/sda
ESP=${DISK}18
WIN=${DISK}19
SYS=/dev/block/platform/155a0000.ufs/by-name/SYSTEM
chmod 755 "$D"/lib/* "$D"/bin/* 2>/dev/null   # adb push from Windows drops the x bit
run() { n=$1; shift; "$D/lib/ld-musl-aarch64.so.1" --library-path "$D/lib" "$D/bin/$n" "$@"; }
die() { echo "HATA: $*"; exit 1; }

case "$1" in
info)
    run sgdisk -p $DISK
    ;;
partition)
    INFO=$(run sgdisk -i 18 $DISK) || die "sgdisk okunamadi"
    echo "$INFO" | grep -q "Partition name: 'USERDATA'" || {
        echo "$INFO"; die "partisyon 18 USERDATA degil, durdum"; }
    FIRST=$(echo "$INFO" | sed -n 's/^First sector: \([0-9]*\).*/\1/p')
    LAST=$(echo "$INFO" | sed -n 's/^Last sector: \([0-9]*\).*/\1/p')
    [ -n "$FIRST" ] && [ -n "$LAST" ] || die "sektorler okunamadi"
    echo "USERDATA: $FIRST - $LAST"
    umount /sdcard /data/media /data 2>/dev/null
    run sgdisk -d 18 \
        -n 18:$FIRST:+300M -t 18:EF00 -c 18:ESP \
        -n 19:0:$LAST -t 19:0700 -c 19:WINDOWS $DISK || die "sgdisk yazamadi"
    run sgdisk -p $DISK | tail -4
    blockdev --rereadpt $DISK 2>/dev/null && echo "TAMAM: tablo yeniden okundu" \
        || echo "Tablo yazildi; cekirdek yeniden okuyamadi -> TWRP'yi yeniden baslat (adb reboot recovery)"
    ;;
format)
    [ -b $WIN ] || die "$WIN yok (once partition, sonra TWRP'yi yeniden baslat)"
    run sgdisk -i 19 $DISK | grep -q "Partition name: 'WINDOWS'" || die "partisyon 19 WINDOWS degil"
    run sgdisk -i 18 $DISK | grep -q "Partition name: 'ESP'" || die "partisyon 18 ESP degil"
    WSTART=$(run sgdisk -i 19 $DISK | sed -n 's/^First sector: \([0-9]*\).*/\1/p')
    run mkfs.vfat -F 32 -S 4096 -s 1 -n ESP $ESP || die "ESP bicimlenemedi"
    run mkntfs -f -F -s 4096 -p $WSTART -H 255 -S 63 -L Windows $WIN || die "NTFS bicimlenemedi"
    umount /system /system_root 2>/dev/null
    MKE2FS_CONFIG=/dev/null run mke2fs -F -t ext4 -L wimstore $SYS || die "SYSTEM bicimlenemedi"
    mkdir -p /tmp/sys && mount -t ext4 $SYS /tmp/sys || die "SYSTEM baglanamadi"
    echo "TAMAM. Simdi bilgisayardan: adb push install.wim /tmp/sys/"
    ;;
apply)
    [ -f /tmp/sys/install.wim ] || { mkdir -p /tmp/sys; mount -t ext4 $SYS /tmp/sys 2>/dev/null; }
    [ -f /tmp/sys/install.wim ] || die "/tmp/sys/install.wim yok"
    run wimlib-imagex info /tmp/sys/install.wim 1 | head -12
    run wimlib-imagex apply /tmp/sys/install.wim 1 $WIN || die "apply basarisiz"
    sync
    echo "TAMAM: Windows WINDOWS partisyonuna acildi."
    ;;
*)
    echo "kullanim: sh $0 info|partition|format|apply"
    ;;
esac
