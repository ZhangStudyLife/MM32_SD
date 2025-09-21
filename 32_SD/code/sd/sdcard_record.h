// SD卡录制管理模块头文件
#ifndef SDCARD_RECORD_H
#define SDCARD_RECORD_H
#include <stddef.h>
#include <stdint.h>

// 创建下一个数字文件夹，返回文件夹序号，folder_path为完整路径
int sdcard_create_next_record_folder(char *folder_path, size_t path_size);

// 在指定文件夹写入数据，filename为文件名，data为数据指针，size为字节数
int sdcard_record_write(const char *folder_path, const char *filename, const void *data, unsigned int size);

#endif // SDCARD_RECORD_H
