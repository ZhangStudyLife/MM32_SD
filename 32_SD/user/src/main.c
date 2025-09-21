/*********************************************************************************************************************
 * MM32F327X-G8P Opensourec Library 即（MM32F327X-G8P 开源库）是一个基于官方 SDK 接口的第三方开源库
 * Copyright (c) 2022 SEEKFREE 逐飞科技
 *
 * 本文件是 MM32F327X-G8P 开源库的一部分
 *
 * MM32F327X-G8P 开源库 是免费软件
 * 您可以根据自由软件基金会发布的 GPL（GNU General Public License，即 GNU通用公共许可证）的条款
 * 即 GPL 的第3版（即 GPL3.0）或（您选择的）任何后来的版本，重新发布和/或修改它
 *
 * 本开源库的发布是希望它能发挥作用，但并未对其作任何的保证
 * 甚至没有隐含的适销性或适合特定用途的保证
 * 更多细节请参见 GPL
 *
 * 您应该在收到本开源库的同时收到一份 GPL 的副本
 * 如果没有，请参阅<https://www.gnu.org/licenses/>
 *
 * 额外注明：
 * 本开源库使用 GPL3.0 开源许可证协议 以上许可申明为译文版本
 * 许可申明英文版在 libraries/doc 文件夹下的 GPL3_permission_statement.txt 文件中
 * 许可证副本在 libraries 文件夹下 即该文件夹下的 LICENSE 文件
 * 欢迎各位使用并传播本程序 但修改内容时必须保留逐飞科技的版权声明（即本声明）
 *
 * 文件名称          main
 * 公司名称          成都逐飞科技有限公司
 * 版本信息          查看 libraries/doc 文件夹内 version 文件 版本说明
 * 开发环境          IAR 8.32.4 or MDK 5.37
 * 适用平台          MM32F327X_G8P
 * 店铺链接          https://seekfree.taobao.com/
 *
 * 修改记录
 * 日期              作者                备注
 * 2022-08-10        Teternal            first version
 ********************************************************************************************************************/

#include "zf_common_headfile.h"
#include "ff.h"
#include "../../code/sd/sdcard_manager.h"
// SD 卡通信模式显示
#ifdef SDIO
#define SD_MODE_STR "SDIO"
#else
#define SD_MODE_STR "SPI "
#endif

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

// FatFs 文件系统对象
static FATFS fs;
static FIL recFile;			   // 录制用文件
static uint8_t recording = 0;  // 0=空闲 1=录制中
static uint16_t rec_index = 1; // 第几次录像
// 视频帧计数
static uint16_t video_index = 1;
// 上次按键状态
static uint8_t prev_key1 = 0;
// BMP 文件计数索引
static uint16_t img_index = 1;

// 帧尺寸（总钻风 MT9V03X）与原始帧大小
#define FRAME_W (MT9V03X_W)				// 例如 188
#define FRAME_H (MT9V03X_H)				// 例如 120
#define FRAME_BYTES (FRAME_W * FRAME_H) // 22560 字节/帧（8bit 灰度）

// 打印 FatFs 错误码到屏幕，便于定位 REC_ERR
static void show_fr(const char *tag, FRESULT fr)
{
	char buf[32];
	sprintf(buf, "%s:%02d", tag, (int)fr);
	ips200_show_string(0, 48, buf);
}

// 可靠创建目录：忽略已存在，其他错误显示
static void safe_mkdir(const char *path)
{
	FRESULT fr = f_mkdir(path);
	if (fr && fr != FR_EXIST)
		show_fr("MKDIR", fr);
}

// 可靠打开录制文件：优先 1:/cmprs，其次 /cmprs（当 chdrive 到 1: 时可用）
static FRESULT open_record_file(FIL *f, uint16_t index)
{
	char path[40];
	// 确保目录存在（双路径）：避免因目录缺失导致打开失败
	safe_mkdir("1:/cmprs");
	safe_mkdir("/cmprs");
	sprintf(path, "1:/cmprs/%d.dat", index);
	FRESULT fr = f_open(f, path, FA_CREATE_ALWAYS | FA_WRITE);
	if (fr == FR_OK)
		return fr;
	sprintf(path, "/cmprs/%d.dat", index);
	return f_open(f, path, FA_CREATE_ALWAYS | FA_WRITE);
}

/**
 * @brief 将灰度图像保存为 BMP 文件
 * @param path BMP 文件完整路径（如 "1:/img/1.bmp"）
 * @return FatFs 操作结果 FR_OK 表示成功
 */
FRESULT save_bmp(const char *path)
{
	FIL file;
	UINT bw;
	BITMAPFILEHEADER fh;
	BITMAPINFOHEADER ih;
	RGBQUAD palette[256];
	uint32_t rowSize = ((MT9V03X_W + 3) / 4) * 4;
	uint32_t imgSize = rowSize * MT9V03X_H;
	uint32_t offsetBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + sizeof(palette);

	// 文件头
	fh.bfType = 0x4D42;
	fh.bfSize = offsetBits + imgSize;
	fh.bfReserved1 = 0;
	fh.bfReserved2 = 0;
	fh.bfOffBits = offsetBits;

	// 信息头
	ih.biSize = sizeof(BITMAPINFOHEADER);
	ih.biWidth = MT9V03X_W;
	ih.biHeight = MT9V03X_H;
	ih.biPlanes = 1;
	ih.biBitCount = 8;
	ih.biCompression = 0;
	ih.biSizeImage = imgSize;
	ih.biXPelsPerMeter = 0;
	ih.biYPelsPerMeter = 0;
	ih.biClrUsed = 256;
	ih.biClrImportant = 0;

	// 调色板（灰度）
	for (int i = 0; i < 256; i++)
	{
		palette[i].rgbBlue = i;
		palette[i].rgbGreen = i;
		palette[i].rgbRed = i;
		palette[i].rgbReserved = 0;
	}

	// 打开文件
	if (f_open(&file, path, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK)
	{
		return FR_DISK_ERR;
	}
	// 写入头部
	f_write(&file, &fh, sizeof(fh), &bw);
	f_write(&file, &ih, sizeof(ih), &bw);
	// 写入调色板
	f_write(&file, palette, sizeof(palette), &bw);

	// 写入像素数据：自底向上
	for (int y = MT9V03X_H - 1; y >= 0; y--)
	{
		f_write(&file, image_copy[y], MT9V03X_W, &bw);
		// 行对齐填充
		uint8_t pad[4] = {0};
		f_write(&file, pad, rowSize - MT9V03X_W, &bw);
	}
	f_close(&file);
	return FR_OK;
}

// 打开新的工程或者工程移动了位置务必执行以下操作
// 第一步 关闭上面所有打开的文件
// 第二步 project->clean  等待下方进度条走完

// *************************** 例程硬件连接说明 ***************************
// 核心板正常供电即可 无需额外连接
// 如果使用主板测试 主板必须要用电池供电

// *************************** 例程测试说明 ***************************
// 1.核心板烧录完成本例程 完成上电
//
// 2.可以看到核心板上两个 LED 呈流水灯状闪烁
//
// 3.将 SWITCH1 / SWITCH2 两个宏定义对应的引脚分别按照 00 01 10 11 的组合接到 1-VCC 0-GND 或者波动对应主板的拨码开关
//
// 3.不同的组合下 两个 LED 流水灯状闪烁的频率会发生变化
//
// 4.将 KEY1 / KEY2 / KEY3 / KEY4 两个宏定义对应的引脚接到 1-VCC 0-GND 或者 按对应按键
//
// 5.任意引脚接 GND 或者 按键按下会使得两个 LED 一起闪烁 松开后恢复流水灯
//
// 如果发现现象与说明严重不符 请参照本文件最下方 例程常见问题说明 进行排查

// **************************** 代码区域 ****************************
#define LED1 (H2)
#define LED2 (B13)

#define KEY1 (E2)
#define KEY2 (E3)
#define KEY3 (E4)
#define KEY4 (E5)

#define SWITCH1 (D3)
#define SWITCH2 (D4)

int32 encoder1;
int32 encoder2;
int8 offset = 0;

uint32 key1_count;
uint32 key2_count;
uint32 key3_count;
uint32 key4_count;
uint8 key1_flag;
uint8 key2_flag;
uint8 key3_flag;
uint8 key4_flag;
uint32 count_time = 1000;

void all_init(void)
{
	clock_init(SYSTEM_CLOCK_120M); // 初始化芯片时钟 工作频率为 120MHz
	debug_init();				   // 初始化默认 Debug UART

	system_delay_ms(300);

	ips200_init(IPS200_TYPE_SPI);

	while (1)
	{
		if (mt9v03x_init())
		{
			ips200_show_string(0, 16, "mt9v03x reinit.");
		}
		else
		{
			break;
		}
		system_delay_ms(500); // 短延时快速闪灯表示异常
	}
	ips200_show_string(0, 16, "init success.");
	system_delay_ms(1000);
	gpio_init(KEY1, GPI, GPIO_HIGH, GPI_PULL_UP); // 初始化 KEY1 输入 默认高电平 上拉输入
	gpio_init(KEY2, GPI, GPIO_HIGH, GPI_PULL_UP); // 初始化 KEY2 输入 默认高电平 上拉输入
	gpio_init(KEY3, GPI, GPIO_HIGH, GPI_PULL_UP); // 初始化 KEY3 输入 默认高电平 上拉输入
	gpio_init(KEY4, GPI, GPIO_HIGH, GPI_PULL_UP); // 初始化 KEY4 输入 默认高电平 上拉输入

	gpio_init(LED1, GPO, GPIO_HIGH, GPO_PUSH_PULL); // 初始化 LED1 输出 默认高电平 推挽输出模式
	gpio_init(LED2, GPO, GPIO_HIGH, GPO_PUSH_PULL); // 初始化 LED2 输出 默认高电平 推挽输出模式

	encoder_quad_init(TIM3_ENCODER, TIM3_ENCODER_CH1_B4, TIM3_ENCODER_CH2_B5);
	encoder_dir_init(TIM4_ENCODER, TIM4_ENCODER_CH1_B6, TIM4_ENCODER_CH2_B7);

	ips200_clear();

	pit_ms_init(TIM2_PIT, 1);
	pit_ms_init(TIM5_PIT, 100);
	interrupt_set_priority(TIM2_IRQn, 0);
	interrupt_set_priority(TIM5_IRQn, 1);
	// 挂载 SD 卡文件系统，驱动号为 1（失败则显示错误码并重试）
	// 挂载与必要目录准备（统一到 sdcard_manager 内处理）
	sdcard_init_and_mount();
	// 目录创建已移至 sdcard_manager，仅保留挂载

	// 挂载完成后显示 SD 模式
	ips200_show_string(0, 32, SD_MODE_STR);
}

void display(void)
{
	//	ips200_show_chinese(0, 100+offset, 16, test_chinese1[0], 14, RGB565_RED);
	//	ips200_show_chinese(0, 116+offset, 16, test_chinese2[0], 14, RGB565_RED);
	ips200_show_chinese(0, 132 + offset, 16, test_chinese3[0], 7, RGB565_RED);
	ips200_show_chinese(0, 148 + offset, 16, test_chinese4[0], 7, RGB565_RED);
	ips200_show_int(115, 132 + offset, encoder1, 3);
	ips200_show_int(115, 148 + offset, encoder2, 3);
	ips200_show_string(0, 164 + offset, "E2:");
	ips200_show_string(0, 180 + offset, "E3:");
	ips200_show_string(0, 196 + offset, "E4:");
	ips200_show_string(0, 212 + offset, "E5:");

	// KEY1 边沿检测并保存 BMP
	{
		static uint8_t prev_key1 = 0;
		uint8_t curr_key1 = !gpio_get_level(KEY1);
		if (curr_key1 && !prev_key1)
		{
			char path[20];
			// 生成文件名，如 "1:/cmprs/1.bmp"
			sprintf(path, "1:/cmprs/%d.bmp", img_index);
			if (save_bmp(path) == FR_OK)
				ips200_show_string(30, 164 + offset, "Save OK");
			else
				ips200_show_string(30, 164 + offset, "Save Err");
			img_index++;
		}
		prev_key1 = curr_key1;
	}
	// KEY2 录制边沿检测：开始/停止录制原始数据（raw .dat）
	{
		static uint8_t prev_key2 = 0;
		uint8_t curr_key2 = !gpio_get_level(KEY2);
		if (curr_key2 && !prev_key2)
		{
			if (!recording)
			{
				// 开始录制：按索引生成文件名并打开
				FRESULT fr = open_record_file(&recFile, rec_index);
				if (fr == FR_OK)
				{
					recording = 1;
					ips200_show_string(30, 180 + offset, "REC ON ");
				}
				else
				{
					show_fr("OPEN", fr);
					ips200_show_string(30, 180 + offset, "REC ERR");
				}
			}
			else
			{
				// 停止录制
				FRESULT fr = f_close(&recFile);
				if (fr)
					show_fr("CLOSE", fr);
				recording = 0;
				rec_index++;
				ips200_show_string(30, 180 + offset, "REC OFF");
			}
		}
		prev_key2 = curr_key2;
	}
	if (!gpio_get_level(KEY3))
	{
		key3_flag = 1;
		key3_count = 0;
		ips200_show_string(30, 196 + offset, "OK");
	}
	else if (!key3_flag)
		ips200_show_string(30, 196 + offset, "  ");
	if (!gpio_get_level(KEY4))
	{
		key4_flag = 1;
		key4_count = 0;
		ips200_show_string(30, 212 + offset, "OK");
	}
	else if (!key4_flag)
		ips200_show_string(30, 212 + offset, "  ");
}

int main(void)
{
	all_init();

	while (1)
	{
		// 录制模式：直接写入原始灰度帧到 .dat，屏蔽屏幕显示以提升吞吐
		if (recording && mt9v03x_finish_flag)
		{
			UINT bw = 0;
			FRESULT fr = f_write(&recFile, mt9v03x_image, FRAME_BYTES, &bw);
			if (fr || bw != FRAME_BYTES)
			{
				show_fr("WRITE", fr);
			}
			mt9v03x_finish_flag = 0;
			continue;
		}
		if (mt9v03x_finish_flag)
		{
			memcpy(image_copy, mt9v03x_image, MT9V03X_H * MT9V03X_W);
			ips200_show_gray_image(0, 0, (const uint8 *)image_copy, MT9V03X_W, MT9V03X_H, MT9V03X_W, MT9V03X_H, 0);
			mt9v03x_finish_flag = 0;
		}

		display();
	}
}
// **************************** 代码区域 ****************************

// *************************** 例程常见问题说明 ***************************
// 遇到问题时请按照以下问题检查列表检查
//
// 问题1：LED 不闪烁
//      如果使用主板测试，主板必须要用电池供电
//      查看程序是否正常烧录，是否下载报错，确认正常按下复位按键
//      万用表测量对应 LED 引脚电压是否变化，如果不变化证明程序未运行，如果变化证明 LED 灯珠损坏
//
// 问题2：SWITCH1 / SWITCH2 更改组合流水灯频率无变化
//      如果使用主板测试，主板必须要用电池供电
//      查看程序是否正常烧录，是否下载报错，确认正常按下复位按键
//      万用表测量对应 LED 引脚电压是否变化，如果不变化证明程序未运行，如果变化证明 LED 灯珠损坏
//      万用表检查对应 SWITCH1 / SWITCH2 引脚电压是否正常变化，是否跟接入信号不符，引脚是否接错
//
// 问题3：KEY1 / KEY2 / KEY3 / KEY4 接GND或者按键按下无变化
//      如果使用主板测试，主板必须要用电池供电
//      查看程序是否正常烧录，是否下载报错，确认正常按下复位按键
//      万用表测量对应 LED 引脚电压是否变化，如果不变化证明程序未运行，如果变化证明 LED 灯珠损坏
//      万用表检查对应 KEY1 / KEY2 / KEY3 / KEY4 引脚电压是否正常变化，是否跟接入信号不符，引脚是否接错
