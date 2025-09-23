/*********************************************************************************************************************
 * MM32F327X-G8P SD卡管理模块头文件
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

#ifndef SD_MANAGER_H
#define SD_MANAGER_H

#include "zf_common_headfile.h"

//********************************************************************************************************************
// SD卡管理模块配置
//********************************************************************************************************************
#define SD_DRIVE_NUMBER "1:"         // SD卡驱动器号
#define SD_COMPRESS_DIR "1:/cmprs"   // 压缩图片目录
#define SD_COMPRESS_DIR_ALT "/cmprs" // 备用目录路径

//********************************************************************************************************************
// 数据结构定义
//********************************************************************************************************************
typedef enum
{
    SD_OK = 0,            // 操作成功
    SD_INIT_FAILED,       // 初始化失败
    SD_MOUNT_FAILED,      // 挂载失败
    SD_CREATE_DIR_FAILED, // 创建目录失败
    SD_OPEN_FILE_FAILED,  // 打开文件失败
    SD_WRITE_FAILED,      // 写入失败
    SD_CLOSE_FAILED       // 关闭文件失败
} sd_result_t;

typedef struct
{
    FATFS fs;              // 文件系统对象
    FIL record_file;       // 录制文件句柄
    uint8_t is_mounted;    // 挂载状态标志
    uint8_t is_recording;  // 录制状态标志
    uint16_t bmp_index;    // BMP文件索引
    uint16_t record_index; // 录制文件索引
    uint16_t frame_width;  // 当前录制帧宽度
    uint16_t frame_height; // 当前录制帧高度
    uint32_t frame_size;   // 当前录制帧大小
} sd_manager_t;

//********************************************************************************************************************
// 外部接口函数声明
//********************************************************************************************************************

/**
 * @brief 初始化SD卡管理器
 * @return sd_result_t 操作结果
 */
sd_result_t sd_manager_init(void);

/**
 * @brief 获取SD卡管理器实例
 * @return sd_manager_t* 管理器指针
 */
sd_manager_t *sd_manager_get_instance(void);

/**
 * @brief 保存BMP图片文件
 * @param image_data 图像数据指针
 * @param width 图像宽度
 * @param height 图像高度
 * @return sd_result_t 操作结果
 */
sd_result_t sd_manager_save_bmp(const uint8_t *image_data, uint16_t width, uint16_t height);

/**
 * @brief 开始录制原始数据
 * @return sd_result_t 操作结果
 */
sd_result_t sd_manager_start_recording(void);

/**
 * @brief 停止录制
 * @return sd_result_t 操作结果
 */
sd_result_t sd_manager_stop_recording(void);

/**
 * @brief 写入原始帧数据
 * @param frame_data 帧数据指针
 * @param frame_size 帧数据大小
 * @return sd_result_t 操作结果
 */
sd_result_t sd_manager_write_frame(const uint8_t *frame_data, uint32_t frame_size);

/**
 * @brief 检查SD卡是否已挂载
 * @return uint8_t 1-已挂载，0-未挂载
 */
uint8_t sd_manager_is_mounted(void);

/**
 * @brief 检查是否正在录制
 * @return uint8_t 1-录制中，0-未录制
 */
uint8_t sd_manager_is_recording(void);

/**
 * @brief 获取错误信息字符串
 * @param result 错误代码
 * @return const char* 错误描述字符串
 */
const char *sd_manager_get_error_string(sd_result_t result);

/**
 * @brief 显示FatFS错误到屏幕
 * @param tag 错误标签
 * @param fr FatFS错误代码
 */
void sd_manager_show_fatfs_error(const char *tag, FRESULT fr);

//********************************************************************************************************************
// 简化录像接口 - 一键录像功能
//********************************************************************************************************************

/**
 * @brief 开始连续录像 - 一键开始，自动处理所有状态
 * @param image_data 图像数据指针
 * @param image_width 图像宽度
 * @param image_height 图像高度
 * @return sd_result_t 操作结果
 * @note 调用此函数后，每次调用都会写入一帧图像到SD卡
 */
sd_result_t sd_manager_start_video_recording(const uint8_t *image_data, uint16_t image_width, uint16_t image_height);

/**
 * @brief 写入一帧图像到录像文件
 * @param image_data 图像数据指针
 * @return sd_result_t 操作结果
 * @note 必须先调用sd_manager_start_video_recording开始录像
 */
sd_result_t sd_manager_record_frame(const uint8_t *image_data);

/**
 * @brief 停止连续录像 - 一键停止，自动关闭文件
 * @return sd_result_t 操作结果
 */
sd_result_t sd_manager_stop_video_recording(void);

#endif // SD_MANAGER_H