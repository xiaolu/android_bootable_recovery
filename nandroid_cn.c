/*
 * Copyright (C) 2014 The CyanogenMod Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <libgen.h>
#include <limits.h>
#include <linux/input.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/limits.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/vfs.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "bootloader.h"
#include "common.h"
#include "cutils/properties.h"
#include "extendedcommands.h"
#include "firmware.h"
#include "flashutils/flashutils.h"
#include "install.h"
#include "libcrecovery/common.h"
#include "minui/minui.h"
#include "minzip/DirUtil.h"
#include "mounts.h"
#include "nandroid.h"
#include "nandroid_md5.h"
#include "recovery_settings.h"
#include "recovery_ui.h"
#include "roots.h"

#define NANDROID_FIELD_DEDUPE_CLEARED_SPACE 1

typedef void (*file_event_callback)(const char* filename);
typedef int (*nandroid_backup_handler)(const char* backup_path, const char* backup_file_image, int callback);
typedef int (*nandroid_restore_handler)(const char* backup_file_image, const char* backup_path, int callback);

static int nandroid_backup_bitfield = 0;
static unsigned int nandroid_files_total = 0;
static unsigned int nandroid_files_count = 0;

#ifdef NEED_SELINUX_FIX
static int nochange;
static int verbose;
static int bakupcon_to_file(const char *pathname, const char *filename)
{
    int ret = 0;
    struct stat sb;
    char* filecontext = NULL;
    FILE * f = NULL;
    if (lstat(pathname, &sb) < 0) {
        LOGW("bakupcon_to_file: %s not found\n", pathname);
        return -1;
    }

    if (lgetfilecon(pathname, &filecontext) < 0) {
        LOGW("bakupcon_to_file: can't get %s context\n", pathname);
        ret = 1;
    }
    else {
        if ((f = fopen(filename, "a+")) == NULL) {
            LOGE("bakupcon_to_file: can't create %s\n", filename);
            return -1;
        }
        //fprintf(f, "chcon -h %s '%s'\n", filecontext, pathname);
        fprintf(f, "%s\t%s\n", pathname, filecontext);
        fclose(f);
        freecon(filecontext);
    }

    //skip read symlink directory
    if (S_ISLNK(sb.st_mode)) return 0;

    DIR *dir = opendir(pathname);
    // not a directory, carry on
    if (dir == NULL) return 0;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        char *entryname;
        if (!strcmp(entry->d_name, ".."))
            continue;
        if (!strcmp(entry->d_name, "."))
            continue;
        if (asprintf(&entryname, "%s/%s", pathname, entry->d_name) == -1)
            continue;
        if ((is_data_media() && (strncmp(entryname, "/data/media/", 12) == 0)) ||
                strncmp(entryname, "/data/data/com.google.android.music/files/", 42) == 0 )
            continue;

        bakupcon_to_file(entryname, filename);
        free(entryname);
    }

    closedir(dir);
    return ret;
}

static int restorecon_from_file(const char *filename)
{
    int ret = 0;
    FILE * f = NULL;
    if ((f = fopen(filename, "r")) == NULL)
    {
        LOGW("restorecon_from_file: can't open %s\n", filename);
        return -1;
    }

    char linebuf[4096];
    while(fgets(linebuf, 4096, f)) {
        if (linebuf[strlen(linebuf)-1] == '\n')
            linebuf[strlen(linebuf)-1] = '\0';

        char *p1, *p2;
        char *buf = linebuf;

        p1 = strtok(buf, "\t");
        if(!p1) continue;
        p2 = strtok(NULL, "\t");
        if(!p2) continue;
        LOGI("%s %s\n", p1, p2);
        if (lsetfilecon(p1, p2) < 0) {
            LOGE("restorecon_from_file: can't setfilecon %s\n", p1);
            ret++;
        }
    }
    fclose(f);
    return ret;
}
#endif

static void nandroid_generate_timestamp_path(char* backup_path) {
    time_t t = time(NULL);
    struct tm *tmp = localtime(&t);
    if (tmp == NULL) {
        struct timeval tp;
        gettimeofday(&tp, NULL);
        snprintf(backup_path, PATH_MAX, "%s/clockworkmod/backup/%ld", get_primary_storage_path(), tp.tv_sec);
    } else {
        char str[PATH_MAX];
        strftime(str, PATH_MAX, "clockworkmod/backup/%F.%H.%M.%S", tmp);
        snprintf(backup_path, PATH_MAX, "%s/%s", get_primary_storage_path(), str);
    }
}

static void ensure_directory(const char* dir) {
    char tmp[PATH_MAX];
    sprintf(tmp, "mkdir -p %s && chmod 777 %s", dir, dir);
    __system(tmp);
}

static int print_and_error(const char* message, int ret) {
    ui_reset_progress();
    ui_set_background(BACKGROUND_ICON_ERROR);
    if (message != NULL)
        LOGE("%s", message); // Assumes message has line termination

    return ret;
}

static void nandroid_callback(const char* filename) {
    if (filename == NULL)
        return;

    char tmp[PATH_MAX];
    strcpy(tmp, filename);
    if (tmp[strlen(tmp) - 1] == '\n')
        tmp[strlen(tmp) - 1] = '\0';
    LOGI("%s\n", tmp);

    if (nandroid_files_total != 0) {
        nandroid_files_count++;
        float progress_decimal = (float)((double)nandroid_files_count /
                                         (double)nandroid_files_total);
        ui_set_progress(progress_decimal);
    }
}

static void compute_directory_stats(const char* directory) {
    char tmp[PATH_MAX];
    char count_text[100];

    // reset file count if we ever return before setting it
    nandroid_files_count = 0;
    nandroid_files_total = 0;

    sprintf(tmp, "find %s | %s wc -l > /tmp/dircount", directory, strcmp(directory, "/data") == 0 && is_data_media() ? "grep -v /data/media |" : "");
    __system(tmp);

    FILE* f = fopen("/tmp/dircount", "r");
    if (f == NULL)
        return;

    if (fgets(count_text, sizeof(count_text), f) == NULL) {
        fclose(f);
        return;
    }

    size_t len = strlen(count_text);
    if (count_text[len - 1] == '\n')
        count_text[len - 1] = '\0';

    fclose(f);
    nandroid_files_total = atoi(count_text);
    ui_reset_progress();
    ui_show_progress(1, 0);
}

static int mkyaffs2image_wrapper(const char* backup_path, const char* backup_file_image, int callback) {
    char tmp[PATH_MAX];
    sprintf(tmp, "cd %s ; mkyaffs2image . %s.img ; exit $?", backup_path, backup_file_image);

    FILE *fp = __popen(tmp, "r");
    if (fp == NULL) {
        ui_print("无法执行mkyaffs2image.\n");
        return -1;
    }

    while (fgets(tmp, PATH_MAX, fp) != NULL) {
        tmp[PATH_MAX - 1] = '\0';
        if (callback)
            nandroid_callback(tmp);
    }

    return __pclose(fp);
}

static int do_tar_compress(char* command, int callback) {
    char buf[PATH_MAX];

    set_perf_mode(1);
    FILE *fp = __popen(command, "r");
    if (fp == NULL) {
        ui_print("无法执行tar命令!\n");
        set_perf_mode(0);
        return -1;
    }

    while (fgets(buf, PATH_MAX, fp) != NULL) {
        buf[PATH_MAX - 1] = '\0';
        if (callback)
            nandroid_callback(buf);
    }

    set_perf_mode(0);
    return __pclose(fp);
}

static int tar_compress_wrapper(const char* backup_path, const char* backup_file_image, int callback) {
    char tmp[PATH_MAX];
    sprintf(tmp, "cd $(dirname %s) ; touch %s.tar ; set -o pipefail ; (tar -cpv --exclude=data/data/com.google.android.music/files/* %s $(basename %s) | split -a 1 -b 1000000000 /proc/self/fd/0 %s.tar.) 2> /proc/self/fd/1 ; exit $?", backup_path, backup_file_image, strcmp(backup_path, "/data") == 0 && is_data_media() ? "--exclude=data/media" : "", backup_path, backup_file_image);

    return do_tar_compress(tmp, callback);
}

static int tar_gzip_compress_wrapper(const char* backup_path, const char* backup_file_image, int callback) {
    char tmp[PATH_MAX];
    sprintf(tmp, "cd $(dirname %s) ; touch %s.tar.gz ; set -o pipefail ; (tar -cpv --exclude=data/data/com.google.android.music/files/* %s $(basename %s) | pigz -c | split -a 1 -b 1000000000 /proc/self/fd/0 %s.tar.gz.) 2> /proc/self/fd/1 ; exit $?", backup_path, backup_file_image, strcmp(backup_path, "/data") == 0 && is_data_media() ? "--exclude=data/media" : "", backup_path, backup_file_image);

    return do_tar_compress(tmp, callback);
}

static int tar_dump_wrapper(const char* backup_path, const char* backup_file_image, int callback) {
    char tmp[PATH_MAX];
    sprintf(tmp, "cd $(dirname %s); set -o pipefail ; tar -cpv --exclude=data/data/com.google.android.music/files/* %s $(basename %s) 2> /dev/null | cat", backup_path, strcmp(backup_path, "/data") == 0 && is_data_media() ? "--exclude=data/media" : "", backup_path);

    return __system(tmp);
}

void nandroid_dedupe_gc(const char* blob_dir) {
    char backup_dir[PATH_MAX];
    strcpy(backup_dir, blob_dir);
    char *d = dirname(backup_dir);
    strcpy(backup_dir, d);
    strcat(backup_dir, "/backup");
    ui_print("正在释放空间...\n");
    char tmp[PATH_MAX];
    sprintf(tmp, "dedupe gc %s $(find %s -name '*.dup')", blob_dir, backup_dir);
    __system(tmp);
    ui_print("释放空间完毕.\n");
}

static int dedupe_compress_wrapper(const char* backup_path, const char* backup_file_image, int callback) {
    char tmp[PATH_MAX];
    char blob_dir[PATH_MAX];
    strcpy(blob_dir, backup_file_image);
    char *d = dirname(blob_dir);
    strcpy(blob_dir, d);
    d = dirname(blob_dir);
    strcpy(blob_dir, d);
    d = dirname(blob_dir);
    strcpy(blob_dir, d);
    strcat(blob_dir, "/blobs");
    ensure_directory(blob_dir);

    if (!(nandroid_backup_bitfield & NANDROID_FIELD_DEDUPE_CLEARED_SPACE)) {
        nandroid_backup_bitfield |= NANDROID_FIELD_DEDUPE_CLEARED_SPACE;
        nandroid_dedupe_gc(blob_dir);
    }

    sprintf(tmp, "dedupe c %s %s %s.dup %s", backup_path, blob_dir, backup_file_image, strcmp(backup_path, "/data") == 0 && is_data_media() ? "./media" : "");

    FILE *fp = __popen(tmp, "r");
    if (fp == NULL) {
        ui_print("无法执行dedupe.\n");
        return -1;
    }

    while (fgets(tmp, PATH_MAX, fp) != NULL) {
        tmp[PATH_MAX - 1] = '\0';
        if (callback)
            nandroid_callback(tmp);
    }

    return __pclose(fp);
}

static void build_configuration_path(char *path_buf, const char *file) {
    //sprintf(path_buf, "%s%s%s", get_primary_storage_path(), (is_data_media() ? "/0/" : "/"), file);
    sprintf(path_buf, "%s/%s", get_primary_storage_path(), file);
}

static nandroid_backup_handler default_backup_handler = tar_compress_wrapper;
static char forced_backup_format[5] = "";
void nandroid_force_backup_format(const char* fmt) {
    strcpy(forced_backup_format, fmt);
}

static void refresh_default_backup_handler() {
    char fmt[5];
    if (strlen(forced_backup_format) > 0) {
        strcpy(fmt, forced_backup_format);
    } else {
        char path[PATH_MAX];

        build_configuration_path(path, NANDROID_BACKUP_FORMAT_FILE);
        ensure_path_mounted(path);
        FILE* f = fopen(path, "r");
        if (NULL == f) {
            default_backup_handler = tar_compress_wrapper;
            return;
        }
        fread(fmt, 1, sizeof(fmt), f);
        fclose(f);
    }

    fmt[3] = '\0';
    if (0 == strcmp(fmt, "dup"))
        default_backup_handler = dedupe_compress_wrapper;
    else if (0 == strcmp(fmt, "tgz"))
        default_backup_handler = tar_gzip_compress_wrapper;
    else if (0 == strcmp(fmt, "tar"))
        default_backup_handler = tar_compress_wrapper;
    else
        default_backup_handler = tar_compress_wrapper;
}

unsigned int nandroid_get_default_backup_format() {
    refresh_default_backup_handler();
    if (default_backup_handler == dedupe_compress_wrapper) {
        return NANDROID_BACKUP_FORMAT_DUP;
    } else if (default_backup_handler == tar_gzip_compress_wrapper) {
        return NANDROID_BACKUP_FORMAT_TGZ;
    } else {
        return NANDROID_BACKUP_FORMAT_TAR;
    }
}

static nandroid_backup_handler get_backup_handler(const char *backup_path) {
    Volume *v = volume_for_path(backup_path);
    if (v == NULL) {
        ui_print("无法找到卷.\n");
        return NULL;
    }
    const MountedVolume *mv = find_mounted_volume_by_mount_point(v->mount_point);
    if (mv == NULL) {
        ui_print("无法找到已挂载的卷: %s\n", v->mount_point);
        return NULL;
    }

    if (strcmp(backup_path, "/data") == 0 && is_data_media()) {
        return default_backup_handler;
    }

    if (strlen(forced_backup_format) > 0)
        return default_backup_handler;

    // Disable tar backups of yaffs2 by default
    char prefer_tar[PROPERTY_VALUE_MAX];
    property_get("ro.cwm.prefer_tar", prefer_tar, "false");
    if (strcmp("yaffs2", mv->filesystem) == 0 && strcmp("false", prefer_tar) == 0) {
        return mkyaffs2image_wrapper;
    }

    return default_backup_handler;
}

static int nandroid_backup_partition_extended(const char* backup_path, const char* mount_point, int umount_when_finished) {
    int ret = 0;
    char name[PATH_MAX];
    char tmp[PATH_MAX];
    strcpy(name, basename(mount_point));

    struct stat file_info;
    build_configuration_path(tmp, NANDROID_HIDE_PROGRESS_FILE);
    ensure_path_mounted(tmp);
    int callback = stat(tmp, &file_info) != 0;

    ui_print("正在备份%s...\n", name);
    if (0 != (ret = ensure_path_mounted(mount_point) != 0)) {
        ui_print("不能挂载%s!\n", mount_point);
        return ret;
    }
    compute_directory_stats(mount_point);
    scan_mounted_volumes();
    Volume *v = volume_for_path(mount_point);
    const MountedVolume *mv = NULL;
    if (v != NULL)
        mv = find_mounted_volume_by_mount_point(v->mount_point);

    if (strcmp(backup_path, "-") == 0)
        sprintf(tmp, "/proc/self/fd/1");
    else if (mv == NULL || mv->filesystem == NULL)
        sprintf(tmp, "%s/%s.auto", backup_path, name);
    else
        sprintf(tmp, "%s/%s.%s", backup_path, name, mv->filesystem);
    nandroid_backup_handler backup_handler = get_backup_handler(mount_point);

    if (backup_handler == NULL) {
        ui_print("无法找到合适的备份方法.\n");
        return -2;
    }
    ret = backup_handler(mount_point, tmp, callback);
    if (umount_when_finished) {
        ensure_path_unmounted(mount_point);
    }
    if (0 != ret) {
        ui_print("备份%s时出错!\n", mount_point);
        return ret;
    }
    ui_print("备份%s完毕.\n", name);
    return 0;
}

static int nandroid_backup_partition(const char* backup_path, const char* root) {
    Volume *vol = volume_for_path(root);
    // make sure the volume exists before attempting anything...
    if (vol == NULL || vol->fs_type == NULL)
        return 0;

    // see if we need a raw backup (mtd)
    char tmp[PATH_MAX];
    int ret;
    if (strcmp(vol->fs_type, "mtd") == 0 ||
            strcmp(vol->fs_type, "bml") == 0 ||
            strcmp(vol->fs_type, "emmc") == 0) {
        const char* name = basename(root);
        if (strcmp(backup_path, "-") == 0)
            strcpy(tmp, "/proc/self/fd/1");
        else
            sprintf(tmp, "%s/%s.img", backup_path, name);

        ui_print("正在备份%s镜像...\n", name);
        if (0 != (ret = backup_raw_partition(vol->fs_type, vol->blk_device, tmp))) {
            ui_print("备份%s镜像时出错!", name);
            return ret;
        }

        ui_print("备份%s镜像完毕.\n", name);
        return 0;
    }

    return nandroid_backup_partition_extended(backup_path, root, 1);
}

int nandroid_backup(const char* backup_path) {
    nandroid_backup_bitfield = 0;
    refresh_default_backup_handler();

    if (ensure_path_mounted(backup_path) != 0) {
        return print_and_error("挂载备份路径出错.\n", NANDROID_ERROR_GENERAL);
    }

    Volume* volume;
    if (is_data_media_volume_path(backup_path))
        volume = volume_for_path("/data");
    else
        volume = volume_for_path(backup_path);
    if (NULL == volume)
        return print_and_error("找不到备份路径对应的卷.\n", NANDROID_ERROR_GENERAL);
    int ret;
    struct statfs sfs;
    struct stat s;
    if (NULL != volume) {
        if (0 != (ret = statfs(volume->mount_point, &sfs)))
            return print_and_error("无法获取备份路径状态.\n", ret);
        uint64_t bavail = sfs.f_bavail;
        uint64_t bsize = sfs.f_bsize;
        uint64_t sdcard_free = bavail * bsize;
        uint64_t sdcard_free_mb = sdcard_free / (uint64_t)(1024 * 1024);
        ui_print("剩余空间: %lluMB\n", sdcard_free_mb);
        if (sdcard_free_mb < 150)
            ui_print("可能没有足够的可用空间来完成备份...继续备份...\n");
    }
    char tmp[PATH_MAX];
    ensure_directory(backup_path);
    ui_set_background(BACKGROUND_ICON_INSTALLING);

    if (0 != (ret = nandroid_backup_partition(backup_path, "/boot")))
        return print_and_error(NULL, ret);

    if (0 != (ret = nandroid_backup_partition(backup_path, "/recovery")))
        return print_and_error(NULL, ret);

    Volume *vol = volume_for_path("/wimax");
    if (vol != NULL && 0 == stat(vol->blk_device, &s)) {
        char serialno[PROPERTY_VALUE_MAX];
        ui_print("正在备份WiMAX...\n");
        serialno[0] = 0;
        property_get("ro.serialno", serialno, "");
        sprintf(tmp, "%s/wimax.%s.img", backup_path, serialno);
        ret = backup_raw_partition(vol->fs_type, vol->blk_device, tmp);
        if (0 != ret)
            return print_and_error("导出WiMAX分区时出错!\n", NANDROID_ERROR_GENERAL);
    }

    if (0 != (ret = nandroid_backup_partition(backup_path, "/system")))
        return print_and_error(NULL, ret);

    if (volume_for_path("/preload") != NULL)
    {
        if (0 != (ret = nandroid_backup_partition(backup_path, "/preload")))
            return print_and_error(NULL, ret);
    }

    if (0 != (ret = nandroid_backup_partition(backup_path, "/data")))
        return print_and_error(NULL, ret);

    if (has_datadata()) {
        if (0 != (ret = nandroid_backup_partition(backup_path, "/datadata")))
            return print_and_error(NULL, ret);
    }

    if (0 != stat(get_android_secure_path(), &s)) {
        ui_print("找不到.android_secure目录. 跳过备份安装到扩展卡上的程序.\n");
    } else {
        if (0 != (ret = nandroid_backup_partition_extended(backup_path, get_android_secure_path(), 0)))
            return print_and_error(NULL, ret);
    }

    if (0 != (ret = nandroid_backup_partition_extended(backup_path, "/cache", 0)))
        return print_and_error(NULL, ret);

    vol = volume_for_path("/sd-ext");
    if (vol == NULL || 0 != stat(vol->blk_device, &s)) {
        LOGI("没有sd-ext分区,跳过.\n");
    } else {
        if (0 != ensure_path_mounted("/sd-ext"))
            LOGI("Could not mount sd-ext. sd-ext backup may not be supported on this device. Skipping backup of sd-ext.\n");
        else if (0 != (ret = nandroid_backup_partition(backup_path, "/sd-ext")))
            return print_and_error(NULL, ret);
    }

    if (0 != (ret = nandroid_backup_md5_gen(backup_path)))
        return print_and_error(NULL, ret);

    sprintf(tmp, "cp /tmp/recovery.log %s/recovery.log", backup_path);
    __system(tmp);

    char base_dir[PATH_MAX];
    strcpy(base_dir, backup_path);
    char *d = dirname(base_dir);
    strcpy(base_dir, d);
    d = dirname(base_dir);
    strcpy(base_dir, d);

    sprintf(tmp, "chmod -R 777 %s ; chmod -R u+r,u+w,g+r,g+w,o+r,o+w %s ; chmod u+x,g+x,o+x %s/backup", backup_path, base_dir, base_dir);
    __system(tmp);

    sprintf(tmp, "if [ -d %s/blobs ]; then chmod u+x,g+x,o+x %s/blobs; fi", base_dir, base_dir);
    __system(tmp);

    sync();
    ui_set_background(BACKGROUND_ICON_CLOCKWORK);
    ui_reset_progress();
    ui_print("\n备份完毕!\n");
    return 0;
}

static int nandroid_dump(const char* partition) {
    // silence our ui_print statements and other logging
    ui_set_log_stdout(0);

    nandroid_backup_bitfield = 0;
    refresh_default_backup_handler();

    // override our default to be the basic tar dumper
    default_backup_handler = tar_dump_wrapper;

    if (strcmp(partition, "boot") == 0) {
        Volume *vol = volume_for_path("/boot");
        // make sure the volume exists before attempting anything...
        if (vol == NULL || vol->fs_type == NULL)
            return 1;
        char cmd[PATH_MAX];
        sprintf(cmd, "cat %s", vol->blk_device);
        return __system(cmd);
        // return nandroid_backup_partition("-", "/boot");
    }

    if (strcmp(partition, "recovery") == 0) {
        return __system("set -o pipefail ; dump_image recovery /proc/self/fd/1 | cat");
    }

    if (strcmp(partition, "data") == 0) {
        return nandroid_backup_partition("-", "/data");
    }

    if (strcmp(partition, "system") == 0) {
        return nandroid_backup_partition("-", "/system");
    }

    return 1;
}

static int unyaffs_wrapper(const char* backup_file_image, const char* backup_path, int callback) {
    char tmp[PATH_MAX];
    sprintf(tmp, "cd %s ; unyaffs %s ; exit $?", backup_path, backup_file_image);
    FILE *fp = __popen(tmp, "r");
    if (fp == NULL) {
        ui_print("无法执行unyaffs命令.\n");
        return -1;
    }

    while (fgets(tmp, PATH_MAX, fp) != NULL) {
        tmp[PATH_MAX - 1] = '\0';
        if (callback)
            nandroid_callback(tmp);
    }

    return __pclose(fp);
}

static int do_tar_extract(char* command, int callback) {
    char buf[PATH_MAX];

    set_perf_mode(1);
    FILE *fp = __popen(command, "r");
    if (fp == NULL) {
        ui_print("无法执行tar命令.\n");
        set_perf_mode(0);
        return -1;
    }

    while (fgets(buf, PATH_MAX, fp) != NULL) {
        buf[PATH_MAX - 1] = '\0';
        if (callback)
            nandroid_callback(buf);
    }

    set_perf_mode(0);
    return __pclose(fp);
}

static int tar_gzip_extract_wrapper(const char* backup_file_image, const char* backup_path, int callback) {
    char tmp[PATH_MAX];
    sprintf(tmp, "cd $(dirname %s) ; set -o pipefail ; cat %s* | pigz -d -c | tar -xpv ; exit $?", backup_path, backup_file_image);

    return do_tar_extract(tmp, callback);
}

static int tar_extract_wrapper(const char* backup_file_image, const char* backup_path, int callback) {
    char tmp[PATH_MAX];
    sprintf(tmp, "cd $(dirname %s) ; set -o pipefail ; cat %s* | tar -xpv ; exit $?", backup_path, backup_file_image);

    return do_tar_extract(tmp, callback);
}

static int dedupe_extract_wrapper(const char* backup_file_image, const char* backup_path, int callback) {
    char tmp[PATH_MAX];
    char blob_dir[PATH_MAX];
    strcpy(blob_dir, backup_file_image);
    char *bd = dirname(blob_dir);
    strcpy(blob_dir, bd);
    bd = dirname(blob_dir);
    strcpy(blob_dir, bd);
    bd = dirname(blob_dir);
    sprintf(tmp, "dedupe x %s %s/blobs %s; exit $?", backup_file_image, bd, backup_path);

    char path[PATH_MAX];
    FILE *fp = __popen(tmp, "r");
    if (fp == NULL) {
        ui_print("无法执行dedupe命令.\n");
        return -1;
    }

    while (fgets(path, PATH_MAX, fp) != NULL) {
        if (callback)
            nandroid_callback(path);
    }

    return __pclose(fp);
}

static int tar_undump_wrapper(const char* backup_file_image, const char* backup_path, int callback) {
    char tmp[PATH_MAX];
    sprintf(tmp, "cd $(dirname %s) ; tar -xpv ", backup_path);

    return __system(tmp);
}

static nandroid_restore_handler get_restore_handler(const char *backup_path) {
    Volume *v = volume_for_path(backup_path);
    if (v == NULL) {
        ui_print("无法定位备份卷.\n");
        return NULL;
    }
    scan_mounted_volumes();
    const MountedVolume *mv = find_mounted_volume_by_mount_point(v->mount_point);
    if (mv == NULL) {
        ui_print("无法找到已经挂载的卷: %s\n", v->mount_point);
        return NULL;
    }

    if (strcmp(backup_path, "/data") == 0 && is_data_media()) {
        return tar_extract_wrapper;
    }

    // Disable tar backups of yaffs2 by default
    char prefer_tar[PROPERTY_VALUE_MAX];
    property_get("ro.cwm.prefer_tar", prefer_tar, "false");
    if (strcmp("yaffs2", mv->filesystem) == 0 && strcmp("false", prefer_tar) == 0) {
        return unyaffs_wrapper;
    }

    return tar_extract_wrapper;
}

static int nandroid_restore_partition_extended(const char* backup_path, const char* mount_point, int umount_when_finished) {
    int ret = 0;
    char* name = basename(mount_point);

    nandroid_restore_handler restore_handler = NULL;
    const char *filesystems[] = { "yaffs2", "ext2", "ext3", "ext4", "vfat", "rfs", "f2fs", "exfat", NULL };
    const char* backup_filesystem = NULL;
    Volume *vol = volume_for_path(mount_point);
    const char *device = NULL;
    if (vol != NULL)
        device = vol->blk_device;

    char tmp[PATH_MAX];
    sprintf(tmp, "%s/%s.img", backup_path, name);
    struct stat file_info;
    if (strcmp(backup_path, "-") == 0) {
        if (vol)
            backup_filesystem = vol->fs_type;
        restore_handler = tar_extract_wrapper;
        strcpy(tmp, "/proc/self/fd/0");
    } else if (0 != (ret = stat(tmp, &file_info))) {
        // can't find the backup, it may be the new backup format?
        // iterate through the backup types
        //printf("couldn't find default\n");
        const char *filesystem;
        int i = 0;
        while ((filesystem = filesystems[i]) != NULL) {
            sprintf(tmp, "%s/%s.%s.img", backup_path, name, filesystem);
            if (0 == (ret = stat(tmp, &file_info))) {
                backup_filesystem = filesystem;
                restore_handler = unyaffs_wrapper;
                break;
            }
            sprintf(tmp, "%s/%s.%s.tar", backup_path, name, filesystem);
            if (0 == (ret = stat(tmp, &file_info))) {
                backup_filesystem = filesystem;
                restore_handler = tar_extract_wrapper;
                break;
            }
            sprintf(tmp, "%s/%s.%s.tar.gz", backup_path, name, filesystem);
            if (0 == (ret = stat(tmp, &file_info))) {
                backup_filesystem = filesystem;
                restore_handler = tar_gzip_extract_wrapper;
                break;
            }
            sprintf(tmp, "%s/%s.%s.dup", backup_path, name, filesystem);
            if (0 == (ret = stat(tmp, &file_info))) {
                backup_filesystem = filesystem;
                restore_handler = dedupe_extract_wrapper;
                break;
            }
            i++;
        }

        if (backup_filesystem == NULL || restore_handler == NULL) {
            ui_print("找不到%s文件. 跳过还原%s.\n", name, mount_point);
            return 0;
        } else {
            printf("Found new backup image: %s\n", tmp);
        }
    }
    // If the fs_type of this volume is "auto" or mount_point is /data
    // and is_data_media, let's revert
    // to using a rm -rf, rather than trying to do a
    // ext3/ext4/whatever format.
    // This is because some phones (like DroidX) will freak out if you
    // reformat the /system or /data partitions, and not boot due to
    // a locked bootloader.
    // Other devices, like the Galaxy Nexus, XOOM, and Galaxy Tab 10.1
    // have a /sdcard symlinked to /data/media.
    // Or of volume does not exist (.android_secure), just rm -rf.
    if (vol == NULL || 0 == strcmp(vol->fs_type, "auto"))
        backup_filesystem = NULL;
    else if (0 == strcmp(vol->mount_point, "/data") && is_data_media())
        backup_filesystem = NULL;

    ensure_directory(mount_point);

    char path[PATH_MAX];
    build_configuration_path(path, NANDROID_HIDE_PROGRESS_FILE);
    ensure_path_mounted(path);
    int callback = stat(path, &file_info) != 0;

    ui_print("正在还原 %s...\n", name);
    if (backup_filesystem == NULL) {
        if (0 != (ret = format_volume(mount_point))) {
            ui_print("格式化%s时出错!\n", mount_point);
            return ret;
        }
    } else if (0 != (ret = format_device(device, mount_point, backup_filesystem))) {
        ui_print("格式化%s时出错!\n", mount_point);
        return ret;
    }

    if (0 != (ret = ensure_path_mounted(mount_point))) {
        ui_print("不能挂载%s!\n", mount_point);
        return ret;
    }

    if (restore_handler == NULL)
        restore_handler = get_restore_handler(mount_point);

    // override restore handler for undump
    if (strcmp(backup_path, "-") == 0) {
        restore_handler = tar_undump_wrapper;
    }

    if (restore_handler == NULL) {
        ui_print("无法找到合适的备份还原方法.\n");
        return -2;
    }

    if (0 != (ret = restore_handler(tmp, mount_point, callback))) {
        ui_print("还原%s时出错!\n", mount_point);
        return ret;
    }
#ifdef NEED_SELINUX_FIX
    if (strcmp(backup_path, "-") == 0 || restore_handler == dedupe_extract_wrapper)
        LOGI("无需恢复selinux context.\n");
    else if (0 == strcmp(mount_point, "/data") ||
                0 == strcmp(mount_point, "/system") ||
                0 == strcmp(mount_point, "/cache"))
    {
        name = basename(mount_point);
        sprintf(tmp, "%s/%s.context", backup_path, name);
        struct stat sb;
        if (lstat(tmp, &sb) == 0) {
            ui_print("恢复selinux context...\n");
            ret = restorecon_from_file(tmp);
            ui_print("selinux context恢复完毕.\n");
        }
    }
#endif

    if (umount_when_finished) {
        ensure_path_unmounted(mount_point);
    }

    return 0;
}

static int nandroid_restore_partition(const char* backup_path, const char* root) {
    Volume *vol = volume_for_path(root);
    // make sure the volume exists...
    if (vol == NULL || vol->fs_type == NULL)
        return 0;

    // see if we need a raw restore (mtd)
    char tmp[PATH_MAX];
    if (strcmp(vol->fs_type, "mtd") == 0 ||
            strcmp(vol->fs_type, "bml") == 0 ||
            strcmp(vol->fs_type, "emmc") == 0) {
        int ret;
        const char* name = basename(root);
        ui_print("还原备份之前清除%s数据...\n", name);
        if (0 != (ret = format_volume(root))) {
            ui_print("清除%s数据时出错!", name);
            return ret;
        }

        if (strcmp(backup_path, "-") == 0)
            strcpy(tmp, backup_path);
        else
            sprintf(tmp, "%s%s.img", backup_path, root);

        ui_print("还原%s镜像...\n", name);
        if (0 != (ret = restore_raw_partition(vol->fs_type, vol->blk_device, tmp))) {
            ui_print("刷入%s镜像时出错!\n", name);
            return ret;
        }
        return 0;
    }
    return nandroid_restore_partition_extended(backup_path, root, 1);
}

int nandroid_restore(const char* backup_path, unsigned char flags) {
    ui_set_background(BACKGROUND_ICON_INSTALLING);
    ui_show_indeterminate_progress();
    nandroid_files_total = 0;
    int ret;

    int restore_boot = ((flags & NANDROID_BOOT) == NANDROID_BOOT);
    int restore_system = ((flags & NANDROID_SYSTEM) == NANDROID_SYSTEM);
    int restore_data = ((flags & NANDROID_DATA) == NANDROID_DATA);
    int restore_cache = ((flags & NANDROID_CACHE) == NANDROID_CACHE);
    int restore_sdext = ((flags & NANDROID_SDEXT) == NANDROID_SDEXT);
    int restore_wimax = ((flags & NANDROID_WIMAX) == NANDROID_WIMAX);

    if (ensure_path_mounted(backup_path) != 0)
        return print_and_error("不能挂载备份目录.\n", NANDROID_ERROR_GENERAL);

    struct stat s;
    char tmp[PATH_MAX];

    ensure_path_mounted("/sdcard");
    if (0 == stat("/sdcard/clockworkmod/.no_md5sum", &s))
        ui_print("跳过文件MD5校验...\n");
    else {
        if (0 != (ret = nandroid_restore_md5_check(backup_path, flags)))
            return print_and_error(NULL, ret);
    }

    if (restore_boot && NULL != volume_for_path("/boot") && 0 != (ret = nandroid_restore_partition(backup_path, "/boot")))
        return print_and_error(NULL, ret);

    //struct stat s;
    Volume *vol = volume_for_path("/wimax");
    if (restore_wimax && vol != NULL && 0 == stat(vol->blk_device, &s)) {
        char serialno[PROPERTY_VALUE_MAX];

        serialno[0] = 0;
        property_get("ro.serialno", serialno, "");
        sprintf(tmp, "%s/wimax.%s.img", backup_path, serialno);

        struct stat st;
        if (0 != stat(tmp, &st)) {
            ui_print("WARNING: WiMAX partition exists, but nandroid\n");
            ui_print("         backup does not contain WiMAX image.\n");
            ui_print("         You should create a new backup to\n");
            ui_print("         protect your WiMAX keys.\n");
        } else {
            ui_print("Erasing WiMAX before restore...\n");
            if (0 != (ret = format_volume("/wimax")))
                return print_and_error("Error while formatting wimax!\n", NANDROID_ERROR_GENERAL);
            ui_print("Restoring WiMAX image...\n");
            if (0 != (ret = restore_raw_partition(vol->fs_type, vol->blk_device, tmp)))
                return print_and_error(NULL, ret);
        }
    }

    if (restore_system && 0 != (ret = nandroid_restore_partition(backup_path, "/system")))
        return print_and_error(NULL, ret);

    if (volume_for_path("/preload") != NULL)
    {
        if (restore_system && 0 != (ret = nandroid_restore_partition(backup_path, "/preload")))
            return print_and_error(NULL, ret);
    }

    if (restore_data && 0 != (ret = nandroid_restore_partition(backup_path, "/data")))
        return print_and_error(NULL, ret);

    if (has_datadata()) {
        if (restore_data && 0 != (ret = nandroid_restore_partition(backup_path, "/datadata")))
            return print_and_error(NULL, ret);
    }

    if (restore_data && 0 != (ret = nandroid_restore_partition_extended(backup_path, get_android_secure_path(), 0)))
        return print_and_error(NULL, ret);

    if (restore_cache && 0 != (ret = nandroid_restore_partition_extended(backup_path, "/cache", 0)))
        return print_and_error(NULL, ret);

    if (restore_sdext && 0 != (ret = nandroid_restore_partition(backup_path, "/sd-ext")))
        return print_and_error(NULL, ret);

    sync();
    ui_set_background(BACKGROUND_ICON_CLOCKWORK);
    ui_reset_progress();
    ui_print("\n还原完毕!\n");
    return 0;
}

static int nandroid_undump(const char* partition) {
    nandroid_files_total = 0;

    int ret;

    if (strcmp(partition, "boot") == 0) {
        Volume *vol = volume_for_path("/boot");
        // make sure the volume exists before attempting anything...
        if (vol == NULL || vol->fs_type == NULL)
            return 1;
        char cmd[PATH_MAX];
        sprintf(cmd, "cat /proc/self/fd/0 > %s", vol->blk_device);
        return __system(cmd);
        // return __system("flash_image boot /proc/self/fd/0");
    }

    if (strcmp(partition, "recovery") == 0) {
        if (0 != (ret = nandroid_restore_partition("-", "/recovery")))
            return ret;
    }

    if (strcmp(partition, "system") == 0) {
        if (0 != (ret = nandroid_restore_partition("-", "/system")))
            return ret;
    }

    if (strcmp(partition, "data") == 0) {
        if (0 != (ret = nandroid_restore_partition("-", "/data")))
            return ret;
    }

    sync();
    return 0;
}

static int nandroid_usage() {
    printf("Usage: nandroid backup\n");
    printf("Usage: nandroid restore <directory>\n");
    printf("Usage: nandroid dump <partition>\n");
    printf("Usage: nandroid undump <partition>\n");
    return 1;
}

static int bu_usage() {
    printf("Usage: bu <fd> backup partition\n");
    printf("Usage: Prior to restore:\n");
    printf("Usage: echo -n <partition> > /tmp/ro.bu.restore\n");
    printf("Usage: bu <fd> restore\n");
    return 1;
}

int bu_main(int argc, char** argv) {
    load_volume_table();

    if (strcmp(argv[2], "backup") == 0) {
        if (argc != 4) {
            return bu_usage();
        }

        int fd = atoi(argv[1]);
        char* partition = argv[3];

        if (fd != STDOUT_FILENO) {
            dup2(fd, STDOUT_FILENO);
            close(fd);
        }

        int ret = nandroid_dump(partition);
        sleep(10);
        return ret;
    } else if (strcmp(argv[2], "restore") == 0) {
        if (argc != 3) {
            return bu_usage();
        }

        int fd = atoi(argv[1]);
        if (fd != STDIN_FILENO) {
            dup2(fd, STDIN_FILENO);
            close(fd);
        }

        char partition[100];
        FILE* f = fopen("/tmp/ro.bu.restore", "r");
        if (f == NULL) {
            printf("cannot open ro.bu.restore\n");
            return bu_usage();
        }

        if (fgets(partition, sizeof(partition), f) == NULL) {
            fclose(f);
            printf("nothing to restore!\n");
            return bu_usage();
        }

        size_t len = strlen(partition);
        if (partition[len - 1] == '\n')
            partition[len - 1] = '\0';

        return nandroid_undump(partition);
    }

    return bu_usage();
}

int nandroid_main(int argc, char** argv) {
    load_volume_table();
    vold_init();
    char backup_path[PATH_MAX];

    if (argc > 3 || argc < 2)
        return nandroid_usage();

    if (strcmp("backup", argv[1]) == 0) {
        if (argc != 2)
            return nandroid_usage();

        nandroid_generate_timestamp_path(backup_path);
        return nandroid_backup(backup_path);
    }

    if (strcmp("restore", argv[1]) == 0) {
        if (argc != 3)
            return nandroid_usage();
        unsigned char flags = NANDROID_BOOT | NANDROID_SYSTEM | NANDROID_DATA
                              | NANDROID_CACHE | NANDROID_SDEXT;
        return nandroid_restore(argv[2], flags);
    }

    if (strcmp("dump", argv[1]) == 0) {
        if (argc != 3)
            return nandroid_usage();
        return nandroid_dump(argv[2]);
    }

    if (strcmp("undump", argv[1]) == 0) {
        if (argc != 3)
            return nandroid_usage();
        return nandroid_undump(argv[2]);
    }

    return nandroid_usage();
}

