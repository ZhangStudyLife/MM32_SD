// SD卡管理模块，封装main函数中的SD卡相关操作
#include "zf_common_headfile.h"
#include "ff.h"

static FATFS fs;

// 显示FatFs错误码
static void show_fr(const char *tag, FRESULT fr)
{
    char buf[32];
    sprintf(buf, "%s:%02d", tag, (int)fr);
    ips200_show_string(0, 48, buf);
}

// 可靠创建目录
static void safe_mkdir(const char *path)
{
    FRESULT fr = f_mkdir(path);
    if (fr && fr != FR_EXIST)
        show_fr("MKDIR", fr);
}

// SD卡初始化与挂载
void sdcard_init_and_mount(void)
{
    for (;;)
    {
        FRESULT fr = f_mount(&fs, "1:/", 1);
        if (fr == FR_OK)
            break;
        show_fr("MOUNT", fr);
        system_delay_ms(500);
    }
#if (FF_FS_RPATH >= 2U)
    {
        FRESULT fr = f_chdrive("1:");
        if (fr)
            show_fr("CHDRV", fr);
    }
#endif
    // 仅在初始化时创建压缩数据目录（cmprs）
    safe_mkdir("1:/cmprs");
    safe_mkdir("/cmprs");
}

// 显示SD卡通信模式
void sdcard_show_mode(void)
{
#ifdef SDIO
    ips200_show_string(0, 32, "SDIO");
#else
    ips200_show_string(0, 32, "SPI ");
#endif
}
