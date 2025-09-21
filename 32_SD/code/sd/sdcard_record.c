// SD卡录制管理模块，每次录制自动创建数字文件夹并存数据
#include "sdcard_manager.h"
#include "ff.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h> // for atoi

// 获取下一个数字文件夹名（如 "1"、"2"...），并创建
int sdcard_create_next_record_folder(char *folder_path, size_t path_size)
{
    DIR dir;
    FILINFO fno;
    int max_index = 0;
    FRESULT fr = f_opendir(&dir, "1:/");
    if (fr != FR_OK)
        return -1;
    for (;;)
    {
        fr = f_readdir(&dir, &fno);
        if (fr != FR_OK || fno.fname[0] == 0)
            break;
        // 判断是否为数字文件夹
        int idx = atoi(fno.fname);
        if (fno.fattrib & AM_DIR && idx > max_index)
            max_index = idx;
    }
    f_closedir(&dir);
    int next_index = max_index + 1;
    snprintf(folder_path, path_size, "1:/%d", next_index);
    fr = f_mkdir(folder_path);
    if (fr != FR_OK && fr != FR_EXIST)
        return -2;
    return next_index;
}

// 示例：在新文件夹中写入数据
int sdcard_record_write(const char *folder_path, const char *filename, const void *data, unsigned int size)
{
    FIL file;
    char path[64];
    snprintf(path, sizeof(path), "%s/%s", folder_path, filename);
    FRESULT fr = f_open(&file, path, FA_WRITE | FA_CREATE_ALWAYS);
    if (fr != FR_OK)
        return -1;
    unsigned int bw;
    fr = f_write(&file, data, size, &bw);
    f_close(&file);
    return (fr == FR_OK && bw == size) ? 0 : -2;
}
