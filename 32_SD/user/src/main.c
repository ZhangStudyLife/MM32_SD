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
#include "../../code/sd_manager/inc/sd_manager.h"
// SD 卡通信模式显示
#ifdef SDIO
#define SD_MODE_STR "SDIO"
#else
#define SD_MODE_STR "SPI "
#endif

// 应用程序变量
// 视频帧计数
static uint16_t video_index = 1;

// 帧尺寸（总钻风 MT9V03X）与原始帧大小
#define FRAME_W (MT9V03X_W)				// 例如 188
#define FRAME_H (MT9V03X_H)				// 例如 120
#define FRAME_BYTES (FRAME_W * FRAME_H) // 22560 字节/帧（8bit 灰度）

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
	interrupt_set_priority(TIM2_IRQn, 1); // 降低定时器优先级
	interrupt_set_priority(TIM5_IRQn, 2);
	interrupt_set_priority(MT9V03X_DMA_IRQN, 0); // 提高摄像头DMA优先级

	// 初始化SD卡管理器
	sd_result_t sd_result = sd_manager_init();
	if (sd_result != SD_OK)
	{
		ips200_show_string(0, 32, "SD Init Failed");
		ips200_show_string(0, 48, sd_manager_get_error_string(sd_result));
	}
	else
	{
		ips200_show_string(0, 32, SD_MODE_STR);
	}
}

void display(void)
{
	//	ips200_show_chinese(0, 100+offset, 16, test_chinese1[0], 14, RGB565_RED);
	//	ips200_show_chinese(0, 116+offset, 16, test_chinese2[0], 14, RGB565_RED);
	ips200_show_chinese(0, 150 + offset, 16, test_chinese3[0], 7, RGB565_RED);
	ips200_show_chinese(0, 166 + offset, 16, test_chinese4[0], 7, RGB565_RED);
	ips200_show_int(115, 150 + offset, encoder1, 3);
	ips200_show_int(115, 166 + offset, encoder2, 3);
	ips200_show_string(0, 182 + offset, "E2:");
	ips200_show_string(0, 198 + offset, "E3:");
	ips200_show_string(0, 214 + offset, "E4:");
	ips200_show_string(0, 230 + offset, "E5:");

	// KEY1 边沿检测并保存 BMP
	{
		static uint8_t prev_key1 = 0;
		uint8_t curr_key1 = !gpio_get_level(KEY1);
		if (curr_key1 && !prev_key1)
		{
			// 若有新帧完成，先更新一次快照，避免保存过程中的断层
			if (!sd_manager_is_recording() && mt9v03x_finish_flag)
			{
				memcpy(image_copy, mt9v03x_image, FRAME_BYTES);
				mt9v03x_finish_flag = 0;
			}

			// 使用SD管理器保存BMP文件
			sd_result_t result = sd_manager_save_bmp((const uint8_t *)image_copy, MT9V03X_W, MT9V03X_H);
			if (result == SD_OK)
				ips200_show_string(30, 182 + offset, "Save OK");
			else
				ips200_show_string(30, 182 + offset, "Save Err");
		}
		prev_key1 = curr_key1;
	}
	// KEY2 录制边沿检测：一键开始/停止录像
	{
		static uint8_t prev_key2 = 0;
		uint8_t curr_key2 = !gpio_get_level(KEY2);
		if (curr_key2 && !prev_key2)
		{
			if (!sd_manager_is_recording())
			{
				// 开始录像 - 获取当前图像并开始录制
				memcpy(image_copy, mt9v03x_image, FRAME_BYTES);
				sd_result_t result = sd_manager_start_video_recording((const uint8_t *)image_copy, MT9V03X_W, MT9V03X_H);
				if (result == SD_OK)
				{
					ips200_show_string(30, 198 + offset, "REC ON ");
					// 录制期间关闭屏幕图像显示：清除上一帧图像
					ips200_clear();
				}
				else
				{
					ips200_show_string(30, 198 + offset, "REC ERR");
				}
			}
			else
			{
				// 停止录像
				sd_result_t result = sd_manager_stop_video_recording();
				if (result != SD_OK)
				{
					ips200_show_string(0, 48, sd_manager_get_error_string(result));
				}
				ips200_show_string(30, 198 + offset, "REC OFF");
			}
		}
		prev_key2 = curr_key2;
	}
	if (!gpio_get_level(KEY3))
	{
		key3_flag = 1;
		key3_count = 0;
		ips200_show_string(30, 214 + offset, "OK");
	}
	else if (!key3_flag)
		ips200_show_string(30, 214 + offset, "  ");
	if (!gpio_get_level(KEY4))
	{
		key4_flag = 1;
		key4_count = 0;
		ips200_show_string(30, 230 + offset, "OK");
	}
	else if (!key4_flag)
		ips200_show_string(30, 230 + offset, "  ");
}

int main(void)
{
	all_init();

	while (1)
	{
		// 检查新帧
		if (mt9v03x_finish_flag)
		{
			if (sd_manager_is_recording())
			{
				// 录像模式：直接写入当前帧
				sd_manager_record_frame((const uint8_t *)mt9v03x_image);
				// 录制时不显示图像，减少CPU负载
			}
			else
			{
				// 非录制时才显示图像和拷贝数据
				memcpy(image_copy, mt9v03x_image, FRAME_BYTES);
				ips200_show_gray_image(0, 20, (const uint8 *)image_copy,
									   MT9V03X_W, MT9V03X_H, MT9V03X_W, MT9V03X_H, 0);
			}

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
