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

ABI=$(cat /default.prop | grep ro.product.cpu.abi= | dd bs=1 skip=19 count=3)
ABI2=$(cat /default.prop | grep ro.product.cpu.abi2= | dd bs=1 skip=20 count=3)

ARCH=arm
if [ "$ABI" = "x86" ]; then ARCH=x86; fi;
if [ "$ABI2" = "x86" ]; then ARCH=x86; fi;

API=$(cat /system/build.prop | grep ro.build.version.sdk= | dd bs=1 skip=21 count=2)
SUMOD=06755
SUGOTE=false
MKSH=/system/bin/mksh
if [ "$API" -eq "$API" ]; then
  if [ "$API" -gt "17" ]; then
      SUMOD=0755
	  SUGOTE=true
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
chattr -i /system/xbin/su
$BIN/chattr.pie -i /system/xbin/su
chattr -i /system/bin/.ext/.su
$BIN/chattr.pie -i /system/bin/.ext/.su
chattr -i /system/xbin/daemonsu
$BIN/chattr.pie -i /system/xbin/daemonsu
chattr -i $INS
$BIN/chattr.pie -i $INS

rm -f /system/bin/su
rm -f /system/xbin/su
rm -f /system/xbin/daemonsu
rm -f /system/xbin/sugote
rm -f /system/xbin/sugote-mksh
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
rm -f /data/dalvik-cache/*com.noshufou.android.su*
rm -f /data/dalvik-cache/*com.koushikdutta.superuser*
rm -f /data/dalvik-cache/*com.mgyun.shua.su*
rm -f /data/dalvik-cache/*Superuser.apk*
rm -f /data/dalvik-cache/*SuperUser.apk*
rm -f /data/dalvik-cache/*superuser.apk*
rm -f /data/dalvik-cache/*eu.chainfire.supersu*
rm -f /data/dalvik-cache/*Supersu.apk*
rm -f /data/dalvik-cache/*SuperSU.apk*
rm -f /data/dalvik-cache/*supersu.apk*
rm -f /data/dalvik-cache/*.oat
rm -f /data/app/com.noshufou.android.su-*
rm -f /data/app/com.koushikdutta.superuser-*
rm -f /data/app/com.mgyun.shua.su-*
rm -f /data/app/eu.chainfire.supersu-*

mkdir /system/bin/.ext
cp $BIN/su /system/xbin/daemonsu
cp $BIN/su /system/xbin/su
if ($SUGOTE); then 
  cp $BIN/su /system/xbin/sugote	
  cp $MKSH /system/xbin/sugote-mksh
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
set_perm 0 0 0755 /system/xbin/daemonsu
set_perm 0 0 0755 $INS
set_perm 0 0 0755 /system/etc/init.d/99SuperSUDaemon
set_perm 0 0 0644 /system/etc/.installed_su_daemon

ch_con /system/bin/.ext/.su
ch_con /system/xbin/su
if ($SUGOTE); then 
    ch_con_ext /system/xbin/sugote u:object_r:zygote_exec:s0
	ch_con /system/xbin/sugote-mksh
fi
ch_con /system/xbin/daemonsu
ch_con $INS
ch_con /system/etc/init.d/99SuperSUDaemon
ch_con /system/etc/.installed_su_daemon

/system/xbin/su --install

umount /system
umount /data

exit 0
