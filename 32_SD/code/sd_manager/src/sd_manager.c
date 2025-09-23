/*********************************************************************************************************************
 * MM32F327X-G8P SD卡管理模块实现文件
 *
 * 功能说明：
 * - SD卡文件系统初始化和挂载
 * - BMP图片保存
 * - 原始数据录制文件管理
 * - 目录创建和文件操作封装
 *
 * 作者：GitHub Copilot
 * 日期：2025-09-23
 *********************************************************************************************************************/

#include "../inc/sd_manager.h"
#include "zf_device_mt9v03x.h"

//********************************************************************************************************************
// 私有变量定义
//********************************************************************************************************************
static sd_manager_t g_sd_manager = {0};

//********************************************************************************************************************
// BMP文件头结构定义
//********************************************************************************************************************
#pragma pack(1)
typedef struct
{
    uint16_t bfType;
    uint32_t bfSize;
    uint16_t bfReserved1;
    uint16_t bfReserved2;
    uint32_t bfOffBits;
} BITMAPFILEHEADER;

typedef struct
{
    uint32_t biSize;
    int32_t biWidth;
    int32_t biHeight;
    uint16_t biPlanes;
    uint16_t biBitCount;
    uint32_t biCompression;
    uint32_t biSizeImage;
    int32_t biXPelsPerMeter;
    int32_t biYPelsPerMeter;
    uint32_t biClrUsed;
    uint32_t biClrImportant;
} BITMAPINFOHEADER;

typedef struct
{
    uint8_t rgbBlue;
    uint8_t rgbGreen;
    uint8_t rgbRed;
    uint8_t rgbReserved;
} RGBQUAD;
#pragma pack()

//********************************************************************************************************************
// 私有函数声明
//********************************************************************************************************************
static sd_result_t sd_create_directory(const char *path);
static sd_result_t sd_create_directories(void);
static FRESULT sd_open_record_file(FIL *f, const char *path);

//********************************************************************************************************************
// 错误信息字符串
//********************************************************************************************************************
static const char *error_strings[] = {
    "Success",
    "Init Failed",
    "Mount Failed",
    "Create Dir Failed",
    "Open File Failed",
    "Write Failed",
    "Close Failed"};

//********************************************************************************************************************
// 公共接口函数实现
//********************************************************************************************************************

/**
 * @brief 初始化SD卡管理器
 */
sd_result_t sd_manager_init(void)
{
    // 清零管理器结构
    memset(&g_sd_manager, 0, sizeof(sd_manager_t));

    // 初始化索引
    g_sd_manager.bmp_index = 1;
    g_sd_manager.record_index = 1;

    // 挂载SD卡文件系统
    FRESULT fr = f_mount(&g_sd_manager.fs, SD_DRIVE_NUMBER, 1);
    if (fr != FR_OK)
    {
        sd_manager_show_fatfs_error("MOUNT", fr);
        return SD_MOUNT_FAILED;
    }

    g_sd_manager.is_mounted = 1;

    // 创建必要的目录
    sd_result_t result = sd_create_directories();
    if (result != SD_OK)
    {
        return result;
    }

    return SD_OK;
}

/**
 * @brief 获取SD卡管理器实例
 */
sd_manager_t *sd_manager_get_instance(void)
{
    return &g_sd_manager;
}

/**
 * @brief 保存BMP图片文件
 */
sd_result_t sd_manager_save_bmp(const uint8_t *image_data, uint16_t width, uint16_t height)
{
    if (!g_sd_manager.is_mounted || !image_data)
    {
        return SD_INIT_FAILED;
    }

    char path[64];
    sprintf(path, "%s/%d.bmp", SD_COMPRESS_DIR, g_sd_manager.bmp_index);

    FIL file;
    UINT bw;

    // 打开文件
    FRESULT fr = f_open(&file, path, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK)
    {
        sd_manager_show_fatfs_error("OPEN_BMP", fr);
        return SD_OPEN_FILE_FAILED;
    }

    // 计算BMP参数
    uint32_t rowSize = ((width + 3) / 4) * 4; // 4字节对齐
    uint32_t imgSize = rowSize * height;

    // 调色板 (256级灰度)
    RGBQUAD palette[256];
    for (int i = 0; i < 256; i++)
    {
        palette[i].rgbBlue = palette[i].rgbGreen = palette[i].rgbRed = i;
        palette[i].rgbReserved = 0;
    }

    uint32_t offsetBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + sizeof(palette);

    // 填充文件头
    BITMAPFILEHEADER fh = {0};
    fh.bfType = 0x4D42; // "BM"
    fh.bfSize = offsetBits + imgSize;
    fh.bfOffBits = offsetBits;

    // 填充信息头
    BITMAPINFOHEADER ih = {0};
    ih.biSize = sizeof(BITMAPINFOHEADER);
    ih.biWidth = width;
    ih.biHeight = height;
    ih.biPlanes = 1;
    ih.biBitCount = 8;
    ih.biCompression = 0;
    ih.biSizeImage = imgSize;

    // 写入文件头、信息头和调色板
    f_write(&file, &fh, sizeof(fh), &bw);
    f_write(&file, &ih, sizeof(ih), &bw);
    f_write(&file, palette, sizeof(palette), &bw);

    // 写入图像数据 (从下到上，BMP格式要求)
    uint8_t pad[4] = {0};
    for (int y = height - 1; y >= 0; y--)
    {
        f_write(&file, image_data + y * width, width, &bw);
        // 行填充到4字节对齐
        f_write(&file, pad, rowSize - width, &bw);
    }

    // 关闭文件
    fr = f_close(&file);
    if (fr != FR_OK)
    {
        sd_manager_show_fatfs_error("CLOSE_BMP", fr);
        return SD_CLOSE_FAILED;
    }

    g_sd_manager.bmp_index++;
    return SD_OK;
}

/**
 * @brief 开始录制原始数据
 */
sd_result_t sd_manager_start_recording(void)
{
    if (!g_sd_manager.is_mounted)
    {
        return SD_INIT_FAILED;
    }

    if (g_sd_manager.is_recording)
    {
        return SD_OK; // 已经在录制中
    }

    char path[64];
    sprintf(path, "1:/cmprs/raw_%d.dat", g_sd_manager.record_index);

    FRESULT fr = sd_open_record_file(&g_sd_manager.record_file, path);
    if (fr != FR_OK)
    {
        sd_manager_show_fatfs_error("OPEN_REC", fr);
        return SD_OPEN_FILE_FAILED;
    }

    g_sd_manager.is_recording = 1;
    return SD_OK;
}

/**
 * @brief 停止录制
 */
sd_result_t sd_manager_stop_recording(void)
{
    if (!g_sd_manager.is_recording)
    {
        return SD_OK; // 没有在录制
    }

    FRESULT fr = f_close(&g_sd_manager.record_file);
    if (fr != FR_OK)
    {
        sd_manager_show_fatfs_error("CLOSE_REC", fr);
        return SD_CLOSE_FAILED;
    }

    g_sd_manager.is_recording = 0;
    g_sd_manager.record_index++;
    return SD_OK;
}

/**
 * @brief 写入原始帧数据
 */
sd_result_t sd_manager_write_frame(const uint8_t *frame_data, uint32_t frame_size)
{
    if (!g_sd_manager.is_recording || !frame_data)
    {
        return SD_INIT_FAILED;
    }

    UINT bw;
    FRESULT fr = f_write(&g_sd_manager.record_file, frame_data, frame_size, &bw);

    if (fr != FR_OK || bw != frame_size)
    {
        sd_manager_show_fatfs_error("WRITE", fr);
        return SD_WRITE_FAILED;
    }

    return SD_OK;
}

/**
 * @brief 检查SD卡是否已挂载
 */
uint8_t sd_manager_is_mounted(void)
{
    return g_sd_manager.is_mounted;
}

/**
 * @brief 检查是否正在录制
 */
uint8_t sd_manager_is_recording(void)
{
    return g_sd_manager.is_recording;
}

/**
 * @brief 获取错误信息字符串
 */
const char *sd_manager_get_error_string(sd_result_t result)
{
    if (result < sizeof(error_strings) / sizeof(error_strings[0]))
    {
        return error_strings[result];
    }
    return "Unknown Error";
}

/**
 * @brief 显示FatFS错误到屏幕
 */
void sd_manager_show_fatfs_error(const char *tag, FRESULT fr)
{
    char buf[32];
    sprintf(buf, "%s:%02d", tag, (int)fr);
    ips200_show_string(0, 48, buf);
}

//********************************************************************************************************************
// 私有函数实现
//********************************************************************************************************************

/**
 * @brief 安全创建目录（忽略已存在错误）
 */
static sd_result_t sd_create_directory(const char *path)
{
    FRESULT fr = f_mkdir(path);
    if (fr != FR_OK && fr != FR_EXIST)
    {
        sd_manager_show_fatfs_error("MKDIR", fr);
        return SD_CREATE_DIR_FAILED;
    }
    return SD_OK;
}

/**
 * @brief 创建所有必要的目录
 */
static sd_result_t sd_create_directories(void)
{
    sd_result_t result;

    // 创建压缩图片目录
    result = sd_create_directory(SD_COMPRESS_DIR);
    if (result != SD_OK)
    {
        return result;
    }

    // 创建备用目录
    result = sd_create_directory(SD_COMPRESS_DIR_ALT);
    if (result != SD_OK)
    {
        return result;
    }

    return SD_OK;
}

/**
 * @brief 打开录制文件
 */
static FRESULT sd_open_record_file(FIL *f, const char *path)
{
    // 先创建目录
    sd_create_directories();

    // 打开文件
    return f_open(f, path, FA_CREATE_ALWAYS | FA_WRITE);
}

//********************************************************************************************************************
// 简化录像接口实现 - 一键录像功能
//********************************************************************************************************************

/**
 * @brief 开始连续录像 - 一键开始，自动处理所有状态
 */
sd_result_t sd_manager_start_video_recording(const uint8_t *image_data, uint16_t image_width, uint16_t image_height)
{
    if (!g_sd_manager.is_mounted || !image_data)
    {
        return SD_INIT_FAILED;
    }

    if (g_sd_manager.is_recording)
    {
        return SD_OK; // 已经在录制中
    }

    // 保存图像尺寸信息
    g_sd_manager.frame_width = image_width;
    g_sd_manager.frame_height = image_height;
    g_sd_manager.frame_size = image_width * image_height;

    // 生成录制文件名
    char path[64];
    sprintf(path, "1:/cmprs/video_%d.dat", g_sd_manager.record_index);

    // 打开录制文件
    FRESULT fr = sd_open_record_file(&g_sd_manager.record_file, path);
    if (fr != FR_OK)
    {
        sd_manager_show_fatfs_error("OPEN_VIDEO", fr);
        return SD_OPEN_FILE_FAILED;
    }

    g_sd_manager.is_recording = 1;

    // 写入第一帧
    return sd_manager_record_frame(image_data);
}

/**
 * @brief 写入一帧图像到录像文件
 */
sd_result_t sd_manager_record_frame(const uint8_t *image_data)
{
    if (!g_sd_manager.is_recording || !image_data)
    {
        return SD_INIT_FAILED;
    }

    UINT bw;
    FRESULT fr = f_write(&g_sd_manager.record_file, image_data, g_sd_manager.frame_size, &bw);

    if (fr != FR_OK || bw != g_sd_manager.frame_size)
    {
        sd_manager_show_fatfs_error("WRITE_FRAME", fr);
        return SD_WRITE_FAILED;
    }

    return SD_OK;
}

/**
 * @brief 停止连续录像 - 一键停止，自动关闭文件
 */
sd_result_t sd_manager_stop_video_recording(void)
{
    if (!g_sd_manager.is_recording)
    {
        return SD_OK; // 没有在录制
    }

    FRESULT fr = f_close(&g_sd_manager.record_file);
    if (fr != FR_OK)
    {
        sd_manager_show_fatfs_error("CLOSE_VIDEO", fr);
        return SD_CLOSE_FAILED;
    }

    g_sd_manager.is_recording = 0;
    g_sd_manager.record_index++;

    return SD_OK;
}