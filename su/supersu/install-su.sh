#!/sbin/sh

set_perm() {
	chown $1.$2 $4
	chown $1:$2 $4
	chmod $3 $4
}

ch_con() {
	/system/bin/toolbox chcon u:object_r:system_file:s0 $1
	chcon u:object_r:system_file:s0 $1
}

ch_con_ext() {
	/system/bin/toolbox chcon $2 $1
	chcon $2 $1
}

mount /system
mount /data
mount -o rw,remount /system
mount -o rw,remount /system /system
mount -o rw,remount /
mount -o rw,remount / /

API=$(cat /system/build.prop | grep "ro.build.version.sdk=" | dd bs=1 skip=21 count=2)
ABI=$(cat /default.prop /system/build.prop | grep -m 1 "ro.product.cpu.abi=" | dd bs=1 skip=19 count=3)
ABILONG=$(cat /default.prop /system/build.prop | grep -m 1 "ro.product.cpu.abi=" | dd bs=1 skip=19)
ABI2=$(cat /default.prop /system/build.prop | grep -m 1 "ro.product.cpu.abi2=" | dd bs=1 skip=20 count=3)
SUMOD=06755
SUGOTE=false
SUPOLICY=false
MKSH=/system/bin/mksh
PIE=
ARCH=arm
if [ "$ABI" = "x86" ]; then ARCH=x86; fi;
if [ "$ABI2" = "x86" ]; then ARCH=x86; fi;
if [ "$API" -eq "$API" ]; then
  if [ "$API" -ge "16" ]; then
    PIE=.pie
    if [ "$ABILONG" = "armeabi-v7a" ]; then ARCH=armv7; fi;
    if [ "$ABI" = "mip" ]; then ARCH=mips; fi;
    if [ "$ABILONG" = "mips" ]; then ARCH=mips; fi;
  fi
  if [ "$API" -ge "17" ]; then
    SUMOD=0755
    SUGOTE=true
  fi
  if [ "$API" -ge "19" ]; then
    SUPOLICY=true
  fi
  if [ "$API" -ge "20" ]; then
    if [ "$ABILONG" = "arm64-v8a" ]; then ARCH=arm64; fi;
    if [ "$ABILONG" = "mips64" ]; then ARCH=mips64; fi;
    if [ "$ABILONG" = "x86_64" ]; then ARCH=x64; fi;
  fi
fi
if [ ! -f $MKSH ]; then
  MKSH=/system/bin/sh
fi

BIN=/sbin/supersu/$ARCH
COM=/sbin/supersu/common
INS=/system/etc/install-recovery.sh
if [ -f /system/etc/install_recovery.sh ]; then
    INS=/system/etc/install_recovery.sh
fi

mv -n $INS /system/etc/install-recovery-2.sh

chmod 0755 $BIN/chattr$PIE
LD_LIBRARY_PATH=/system/lib $BIN/chattr$PIE -i /system/bin/su
LD_LIBRARY_PATH=/system/lib $BIN/chattr$PIE -i /system/xbin/su
LD_LIBRARY_PATH=/system/lib $BIN/chattr$PIE -i /system/bin/.ext/.su
LD_LIBRARY_PATH=/system/lib $BIN/chattr$PIE -i /system/xbin/daemonsu
LD_LIBRARY_PATH=/system/lib $BIN/chattr$PIE -i /system/xbin/sugote
LD_LIBRARY_PATH=/system/lib $BIN/chattr$PIE -i /system/xbin/sugote_mksh
LD_LIBRARY_PATH=/system/lib $BIN/chattr$PIE -i /system/xbin/supolicy
LD_LIBRARY_PATH=/system/lib $BIN/chattr$PIE -i $INS
LD_LIBRARY_PATH=/system/lib $BIN/chattr$PIE -i /system/bin/install-recovery.sh

rm -f /system/bin/su
rm -f /system/xbin/su
rm -f /system/xbin/daemonsu
rm -f /system/xbin/sugote
rm -f /system/xbin/sugote-mksh
rm -f /system/xbin/supolicy
rm -f /system/bin/.ext/.su
rm -f /system/bin/install-recovery.sh
rm -f $INS
rm -f /system/etc/init.d/99SuperSUDaemon
rm -f /system/etc/.installed_su_daemon

rm -f /system/app/Superuser.apk
rm -f /system/app/Superuser.odex
rm -f /system/app/SuperUser.apk
rm -f /system/app/SuperUser.odex
rm -f /system/app/superuser.apk
rm -f /system/app/superuser.odex
rm -f /system/app/Supersu.apk
rm -f /system/app/Supersu.odex
rm -f /system/app/SuperSU.apk
rm -f /system/app/SuperSU.odex
rm -f /system/app/supersu.apk
rm -f /system/app/supersu.odex
rm -f /system/app/VenomSuperUser.apk
rm -f /system/app/VenomSuperUser.odex
rm -f /data/dalvik-cache/*com.noshufou.android.su*
rm -f /data/dalvik-cache/*/*com.noshufou.android.su*
rm -f /data/dalvik-cache/*com.koushikdutta.superuser*
rm -f /data/dalvik-cache/*/*com.koushikdutta.superuser*
rm -f /data/dalvik-cache/*com.mgyun.shua.su*
rm -f /data/dalvik-cache/*/*com.mgyun.shua.su*
rm -f /data/dalvik-cache/*com.m0narx.su*
rm -f /data/dalvik-cache/*/*com.m0narx.su*
rm -f /data/dalvik-cache/*Superuser.apk*
rm -f /data/dalvik-cache/*/*Superuser.apk*
rm -f /data/dalvik-cache/*SuperUser.apk*
rm -f /data/dalvik-cache/*/*SuperUser.apk*
rm -f /data/dalvik-cache/*superuser.apk*
rm -f /data/dalvik-cache/*/*superuser.apk*
rm -f /data/dalvik-cache/*VenomSuperUser.apk*
rm -f /data/dalvik-cache/*/*VenomSuperUser.apk*
rm -f /data/dalvik-cache/*eu.chainfire.supersu*
rm -f /data/dalvik-cache/*/*eu.chainfire.supersu*
rm -f /data/dalvik-cache/*Supersu.apk*
rm -f /data/dalvik-cache/*/*Supersu.apk*
rm -f /data/dalvik-cache/*SuperSU.apk*
rm -f /data/dalvik-cache/*/*SuperSU.apk*
rm -f /data/dalvik-cache/*supersu.apk*
rm -f /data/dalvik-cache/*/*supersu.apk*
rm -f /data/dalvik-cache/*.oat
rm -f /data/app/com.noshufou.android.su*
rm -f /data/app/com.koushikdutta.superuser*
rm -f /data/app/com.mgyun.shua.su*
rm -f /data/app/com.m0narx.su*
rm -f /data/app/eu.chainfire.supersu-*
rm -f /data/app/eu.chainfire.supersu.apk

mkdir /system/bin/.ext
cp $BIN/su /system/xbin/daemonsu
cp $BIN/su /system/xbin/su
if ($SUGOTE); then 
  cp $BIN/su /system/xbin/sugote	
  cp $MKSH /system/xbin/sugote-mksh
fi
if ($SUPOLICY); then
  cp $BIN/supolicy /system/xbin/supolicy
fi
cp $BIN/su /system/bin/.ext/.su
cp $COM/install-recovery.sh $INS
ln -s $INS /system/bin/install-recovery.sh
cp $COM/99SuperSUDaemon /system/etc/init.d/99SuperSUDaemon
echo 1 > /system/etc/.installed_su_daemon

set_perm 0 0 0777 /system/bin/.ext
set_perm 0 0 $SUMOD /system/bin/.ext/.su
set_perm 0 0 $SUMOD /system/xbin/su
if ($SUGOTE); then
  set_perm 0 0 0755 /system/xbin/sugote
  set_perm 0 0 0755 /system/xbin/sugote-mksh
fi
if ($SUPOLICY); then
  set_perm 0 0 0755 /system/xbin/supolicy
fi
set_perm 0 0 0755 /system/xbin/daemonsu
set_perm 0 0 0755 $INS
set_perm 0 0 0755 /system/etc/init.d/99SuperSUDaemon
set_perm 0 0 0644 /system/etc/.installed_su_daemon
set_perm 0 0 0644 /system/app/Superuser.apk

ch_con /system/bin/.ext/.su
ch_con /system/xbin/su
if ($SUGOTE); then 
  ch_con_ext /system/xbin/sugote u:object_r:zygote_exec:s0
  ch_con /system/xbin/sugote-mksh
fi
if ($SUPOLICY); then
  ch_con /system/xbin/supolicy
fi
ch_con /system/xbin/daemonsu
ch_con $INS
ch_con /system/etc/init.d/99SuperSUDaemon
ch_con /system/etc/.installed_su_daemon

LD_LIBRARY_PATH=/system/lib /system/xbin/su --install


umount /system
umount /data

exit 0
