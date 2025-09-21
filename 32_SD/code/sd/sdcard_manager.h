// SD卡管理模块头文件
#ifndef SDCARD_MANAGER_H
#define SDCARD_MANAGER_H

// 初始化并挂载SD卡，创建常用目录
void sdcard_init_and_mount(void);

// 显示SD卡通信模式（SDIO/SPI）
void sdcard_show_mode(void);

#endif // SDCARD_MANAGER_H
