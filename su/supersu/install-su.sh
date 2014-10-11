#!/sbin/sh
#
# SuperSU installer ZIP 
# Copyright (c) 2012-2014 - Chainfire
#
# To install SuperSU properly, aside from cleaning old versions and
# other superuser-type apps from the system, the following files need to
# be installed:
#
# API   source                        target                              chmod   chcon                       required
#
# 7+    common/Superuser.apk          /system/app/Superuser.apk           0644    u:object_r:system_file:s0   gui
#
# 17+   common/install-recovery.sh    /system/etc/install-recovery.sh     0755    *1                          required
# 17+                                 /system/bin/install-recovery.sh     (symlink to /system/etc/...)        required
# *1: same as /system/bin/toolbox: u:object_r:system_file:s0 if API < 20, u:object_r:toolbox_exec:s0 if API >= 20
#
# 7+    ARCH/su                       /system/xbin/su                     *2      u:object_r:system_file:s0   required
# 7+                                  /system/bin/.ext/.su                *2      u:object_r:system_file:s0   gui
# 17+                                 /system/xbin/daemonsu               0755    u:object_r:system_file:s0   required
# 17+                                 /system/xbin/sugote                 0755    u:object_r:zygote_exec:s0   required
# *2: 06755 if API < 18, 0755 if API >= 18
#
# 19+   ARCH/supolicy                 /system/xbin/supolicy               0755    u:object_r:system_file:s0   required
#
# 17+   /system/bin/sh or mksh *3     /system/xbin/sugote-mksh            0755    u:object_r:system_file:s0   required
# *3: which one (or both) are available depends on API
#
# 17+   common/99SuperSUDaemon *4     /system/etc/init.d/99SuperSUDaemon  0755    u:object_r:system_file:s0   optional
# *4: only place this file if /system/etc/init.d is present
#
# 17+   'echo 1 >' or 'touch' *5      /system/etc/.installed_su_daemon    0644    u:object_r:system_file:s0   optional
# *5: the file just needs to exist or some recoveries will nag you. Even with it there, it may still happen.
#
# It may seem some files are installed multiple times needlessly, but
# it only seems that way. Installing files differently or symlinking
# instead of copying (unless specified) will lead to issues eventually.
#
# The following su binary versions are included in the full package. Each
# should be installed only if the system has the same or newer API level 
# as listed. The script may fall back to a different binary on older API
# levels. supolicy are all ndk/pie/19+ for 32 bit, ndk/pie/20+ for 64 bit.
#
# binary        ARCH/path   build type      API
#
# arm-v5te      arm         aosp static     7+
# x86           x86         aosp static     7+
#
# arm-v7a       armv7       ndk pie         16+
# mips          mips        ndk pie         16+
#
# arm64-v8a     arm64       ndk pie         20+
# mips64        mips64      ndk pie         20+
# x86_64        x64         ndk pie         20+
#
# Note that if SELinux is set to enforcing, the daemonsu binary expects 
# to be run at startup (usually from install-recovery.sh or 
# 99SuperSUDaemon) from u:r:init:s0 or u:r:kernel:s0 contexts. Depending
# on the current policies, it can also deal with u:r:init_shell:s0 and
# u:r:toolbox:s0 contexts. Any other context will lead to issues eventually.
#
# After installation, run '/system/xbin/su --install', which may need to
# perform some additional installation steps. Ideally, at one point,
# a lot of this script will be moved there.
#
# The included chattr(.pie) binaries are used to remove ext2's immutable
# flag on some files. This flag is no longer set by SuperSU's OTA
# survival since API level 18, so there is no need for the 64 bit versions.
# Note that chattr does not need to be installed to the system, it's just
# used by this script, and not supported by the busybox used in older
# recoveries.
#
# Non-static binaries are supported to be PIE (Position Independent 
# Executable) from API level 16, and required from API level 20 (which will
# refuse to execute non-static non-PIE). 
#
# The script performs serveral actions in various ways, sometimes
# multiple times, due to different recoveries and firmwares behaving
# differently, and it thus being required for the correct result.

set_perm() {
  chown $1.$2 $4
  chown $1:$2 $4
  chmod $3 $4
}

ch_con() {
  LD_LIBRARY_PATH=/system/lib /system/toolbox chcon u:object_r:system_file:s0 $1
  LD_LIBRARY_PATH=/system/lib /system/bin/toolbox chcon u:object_r:system_file:s0 $1
  chcon u:object_r:system_file:s0 $1
}

ch_con_ext() {
  LD_LIBRARY_PATH=/system/lib /system/toolbox chcon $2 $1
  LD_LIBRARY_PATH=/system/lib /system/bin/toolbox chcon $2 $1
  chcon $2 $1
}

mount /system
mount /data
mount -o rw,remount /system
mount -o rw,remount /system /system
mount -o rw,remount /
mount -o rw,remount / /

cat /system/bin/toolbox > /system/toolbox
chmod 0755 /system/toolbox

API=$(cat /system/build.prop | grep "ro.build.version.sdk=" | dd bs=1 skip=21 count=2)
ABI=$(cat /default.prop /system/build.prop | grep -m 1 "ro.product.cpu.abi=" | dd bs=1 skip=19 count=3)
ABILONG=$(cat /default.prop /system/build.prop | grep -m 1 "ro.product.cpu.abi=" | dd bs=1 skip=19)
ABI2=$(cat /default.prop /system/build.prop | grep -m 1 "ro.product.cpu.abi2=" | dd bs=1 skip=20 count=3)
SUMOD=06755
SUGOTE=false
SUPOLICY=false
INSTALL_RECOVERY_CONTEXT=u:object_r:system_file:s0
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
    if [ "$(ls -lZ /system/bin/toolbox | grep toolbox_exec > /dev/null; echo $?)" -eq "0" ]; then 
      INSTALL_RECOVERY_CONTEXT=u:object_r:toolbox_exec:s0
    fi
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
ch_con_ext $INS $INSTALL_RECOVERY_CONTEXT
ch_con /system/etc/init.d/99SuperSUDaemon
ch_con /system/etc/.installed_su_daemon

rm /system/toolbox
LD_LIBRARY_PATH=/system/lib /system/xbin/su --install


umount /system
umount /data

exit 0
