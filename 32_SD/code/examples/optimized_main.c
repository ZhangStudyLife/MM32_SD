// 优化后的主函数示例 - 针对帧率优化
#include "zf_common_headfile.h"
#include "ff.h"
#include "../../code/sd/sdcard_manager.h"
#include "../../code/sd/optimized_recorder.h"

// 原有定义保持不变
#define LED1 (H2)
#define LED2 (B13)
#define KEY1 (E2)
#define KEY2 (E3)
#define KEY3 (E4)
#define KEY4 (E5)

// 帧尺寸
#define FRAME_W (MT9V03X_W)
#define FRAME_H (MT9V03X_H)
#define FRAME_BYTES (FRAME_W * FRAME_H)

// 全局变量
static recorder_config_t rec_config;
static uint16_t rec_index = 1;
static uint8_t prev_key1 = 0, prev_key2 = 0;
static uint16_t img_index = 1;

// 性能统计
static uint32_t frames_processed = 0;
static uint32_t frames_dropped = 0;
static uint32_t last_fps_time = 0;
static uint32_t current_fps = 0;

// 优化的初始化函数
void optimized_init(void)
{
    clock_init(SYSTEM_CLOCK_120M);
    debug_init();
    system_delay_ms(300);

    ips200_init(IPS200_TYPE_SPI);

    // 摄像头初始化
    while (1)
    {
        if (mt9v03x_init())
        {
            ips200_show_string(0, 16, "MT9V03X reinit...");
        }
        else
        {
            break;
        }
        system_delay_ms(500);
    }
    ips200_show_string(0, 16, "Camera OK        ");

    // GPIO初始化
    gpio_init(KEY1, GPI, GPIO_HIGH, GPI_PULL_UP);
    gpio_init(KEY2, GPI, GPIO_HIGH, GPI_PULL_UP);
    gpio_init(KEY3, GPI, GPIO_HIGH, GPI_PULL_UP);
    gpio_init(KEY4, GPI, GPIO_HIGH, GPI_PULL_UP);
    gpio_init(LED1, GPO, GPIO_HIGH, GPO_PUSH_PULL);
    gpio_init(LED2, GPO, GPIO_HIGH, GPO_PUSH_PULL);

    // SD卡初始化
    sdcard_init_and_mount();
    ips200_show_string(0, 32, "SD Card OK");

    // 录像器初始化
    rec_config.base_path = "1:/cmprs";
    rec_config.file_index = rec_index;
    rec_config.max_frames = 0; // 无限制
    rec_config.enable_compression = 0;

    if (recorder_init(&rec_config) != RECORDER_IDLE)
    {
        ips200_show_string(0, 48, "Recorder Error");
    }
    else
    {
        ips200_show_string(0, 48, "Recorder OK");
    }

    ips200_clear();

    // 启动性能计时器
    pit_ms_init(TIM2_PIT, 10); // 10ms定时器用于性能统计
    last_fps_time = system_getval_ms();
}

// 快速BMP保存（简化版，减少写入次数）
FRESULT quick_save_bmp(const char *path, const uint8_t *image_data)
{
    FIL file;
    UINT bw;

    // 计算总大小
    uint32_t rowSize = ((MT9V03X_W + 3) / 4) * 4;
    uint32_t imgSize = rowSize * MT9V03X_H;
    uint32_t headerSize = 54 + 256 * 4; // 文件头 + 调色板
    uint32_t totalSize = headerSize + imgSize;

    // 分配一个大缓冲区来减少写入次数
    uint8_t *buffer = zf_malloc(totalSize);
    if (!buffer)
        return FR_NOT_ENOUGH_CORE;

    uint8_t *ptr = buffer;

    // 构建BMP头（简化）
    // ... (省略详细的BMP头构建代码，实际应用中需要完整实现)

    // 一次性写入
    if (f_open(&file, path, FA_CREATE_ALWAYS | FA_WRITE) == FR_OK)
    {
        f_write(&file, buffer, totalSize, &bw);
        f_close(&file);
    }

    zf_free(buffer);
    return (bw == totalSize) ? FR_OK : FR_DISK_ERR;
}

// 性能监控和显示
void update_performance_stats(void)
{
    uint32_t current_time = system_getval_ms();
    if (current_time - last_fps_time >= 1000)
    { // 每秒更新一次
        current_fps = frames_processed;
        frames_processed = 0;
        last_fps_time = current_time;

        // 显示性能信息
        char buf[32];
        sprintf(buf, "FPS: %lu", current_fps);
        ips200_show_string(0, 200, buf);

        sprintf(buf, "Drop: %lu", frames_dropped);
        ips200_show_string(80, 200, buf);

        sprintf(buf, "Rec: %lu", recorder_get_frame_count());
        ips200_show_string(0, 216, buf);
    }
}

// 优化的按键处理
void handle_keys(void)
{
    // KEY1: 保存BMP（优化版）
    uint8_t curr_key1 = !gpio_get_level(KEY1);
    if (curr_key1 && !prev_key1)
    {
        char path[32];
        sprintf(path, "1:/cmprs/%d.bmp", img_index++);

        // 使用当前图像数据直接保存
        if (quick_save_bmp(path, (uint8_t *)mt9v03x_image) == FR_OK)
        {
            ips200_show_string(100, 164, "BMP OK");
        }
        else
        {
            ips200_show_string(100, 164, "BMP ERR");
        }
    }
    prev_key1 = curr_key1;

    // KEY2: 录制控制
    uint8_t curr_key2 = !gpio_get_level(KEY2);
    if (curr_key2 && !prev_key2)
    {
        recorder_state_t state = recorder_get_state();

        if (state == RECORDER_IDLE)
        {
            rec_config.file_index = rec_index++;
            recorder_init(&rec_config);

            if (recorder_start() == RECORDER_RECORDING)
            {
                ips200_show_string(0, 180, "REC START");
                gpio_set_level(LED1, GPIO_LOW); // 录制指示灯
            }
            else
            {
                ips200_show_string(0, 180, "REC ERROR");
            }
        }
        else if (state == RECORDER_RECORDING)
        {
            recorder_stop();
            ips200_show_string(0, 180, "REC STOP ");
            gpio_set_level(LED1, GPIO_HIGH);
        }
    }
    prev_key2 = curr_key2;
}

// 优化的主循环
int main(void)
{
    optimized_init();

    while (1)
    {
        // 检查是否有新帧
        if (mt9v03x_finish_flag)
        {
            frames_processed++;

            // 录制处理（非阻塞）
            if (recorder_get_state() == RECORDER_RECORDING)
            {
                // 直接使用摄像头缓冲区，避免额外拷贝
                if (recorder_add_frame((uint8_t *)mt9v03x_image, FRAME_BYTES) != RECORDER_RECORDING)
                {
                    frames_dropped++;
                }

                // 录制期间不显示图像，减少处理负载
            }
            else
            {
                // 非录制时显示图像
                ips200_show_gray_image(0, 0, (const uint8 *)mt9v03x_image,
                                       MT9V03X_W, MT9V03X_H, MT9V03X_W, MT9V03X_H, 0);
            }

            mt9v03x_finish_flag = 0;
        }

        // 异步处理SD卡写入
        recorder_task();

        // 按键处理
        handle_keys();

        // 性能统计更新
        update_performance_stats();

        // 避免过度占用CPU
        // system_delay_us(100); // 可选：轻微延时
    }
}