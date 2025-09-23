/*********************************************************************************************************************
 * SD卡管理模块封装总结
 * 
 * 日期：2025-09-23
 * 作者：GitHub Copilot
 *********************************************************************************************************************/

# SD卡管理模块封装完成

## 新增文件
1. **d:\smartcar\MM32_SD\32_SD\user\inc\sd_manager.h** - SD卡管理模块头文件
2. **d:\smartcar\MM32_SD\32_SD\user\src\sd_manager.c** - SD卡管理模块实现文件

## 主要功能封装

### 1. SD卡初始化和挂载
- **函数**：`sd_manager_init()`
- **功能**：自动挂载SD卡文件系统，创建必要目录
- **返回**：sd_result_t 错误码

### 2. BMP图片保存
- **函数**：`sd_manager_save_bmp(image_data, width, height)`
- **功能**：将灰度图像保存为标准BMP文件
- **特点**：自动生成文件名，支持256级灰度调色板

### 3. 原始数据录制
- **开始录制**：`sd_manager_start_recording()`
- **写入帧数据**：`sd_manager_write_frame(frame_data, frame_size)`
- **停止录制**：`sd_manager_stop_recording()`

### 4. 状态查询
- **挂载状态**：`sd_manager_is_mounted()`
- **录制状态**：`sd_manager_is_recording()`

### 5. 错误处理
- **错误信息**：`sd_manager_get_error_string(result)`
- **FatFS错误显示**：`sd_manager_show_fatfs_error(tag, fr)`

## 原有代码简化

### 删除的函数和变量
```c
// 删除的函数
static void show_fr(const char *tag, FRESULT fr);
static void safe_mkdir(const char *path);
static FRESULT open_record_file(FIL *f, uint16_t index);
FRESULT save_bmp(const char *path);

// 删除的变量
static FATFS fs;
static FIL recFile;
static uint16_t rec_index;
static uint16_t img_index;
static uint8_t prev_key1;

// 删除的BMP结构体定义
typedef struct BITMAPFILEHEADER;
typedef struct BITMAPINFOHEADER;
typedef struct RGBQUAD;
```

### 简化的主要代码

#### 初始化代码
```c
// 原来：
sdcard_init_and_mount();

// 现在：
sd_result_t sd_result = sd_manager_init();
if (sd_result != SD_OK) {
    ips200_show_string(0, 32, "SD Init Failed");
    ips200_show_string(0, 48, sd_manager_get_error_string(sd_result));
} else {
    ips200_show_string(0, 32, SD_MODE_STR);
}
```

#### BMP保存代码
```c
// 原来：
sprintf(path, "1:/cmprs/%d.bmp", img_index);
if (save_bmp(path) == FR_OK)
    ips200_show_string(30, 182 + offset, "Save OK");
else
    ips200_show_string(30, 182 + offset, "Save Err");
img_index++;

// 现在：
sd_result_t result = sd_manager_save_bmp(image_copy, MT9V03X_W, MT9V03X_H);
if (result == SD_OK)
    ips200_show_string(30, 182 + offset, "Save OK");
else
    ips200_show_string(30, 182 + offset, "Save Err");
```

#### 录制控制代码
```c
// 原来：
FRESULT fr = open_record_file(&recFile, rec_index);
if (fr == FR_OK) {
    recording = 1;
    ips200_show_string(30, 198 + offset, "REC ON ");
    ips200_clear();
} else {
    show_fr("OPEN", fr);
    ips200_show_string(30, 198 + offset, "REC ERR");
}

// 现在：
sd_result_t result = sd_manager_start_recording();
if (result == SD_OK) {
    recording = 1;
    ips200_show_string(30, 198 + offset, "REC ON ");
    ips200_clear();
} else {
    ips200_show_string(30, 198 + offset, "REC ERR");
}
```

#### 异步写入代码
```c
// 原来：
UINT bw = 0;
FRESULT fr = f_write(&recFile, read_buffer, FRAME_BYTES, &bw);
if (fr || bw != FRAME_BYTES) {
    show_fr("WRITE", fr);
}

// 现在：
sd_result_t result = sd_manager_write_frame(read_buffer, FRAME_BYTES);
if (result != SD_OK) {
    ips200_show_string(0, 48, sd_manager_get_error_string(result));
}
```

## 优势

### 1. 代码组织更清晰
- SD卡相关功能集中管理
- 接口统一，易于维护
- 错误处理标准化

### 2. 复用性更强
- 模块化设计，可在其他项目中重用
- 接口抽象，隐藏实现细节
- 配置集中化

### 3. 易于扩展
- 添加新的文件操作功能更容易
- 支持不同的文件格式
- 错误处理可以进一步完善

### 4. 代码更简洁
- main.c文件减少了约150行代码
- 消除了重复的错误处理代码
- 接口调用更直观

## 使用示例

```c
#include "sd_manager.h"

// 初始化
if (sd_manager_init() == SD_OK) {
    // SD卡准备就绪
    
    // 保存图片
    sd_manager_save_bmp(image_data, width, height);
    
    // 开始录制
    if (sd_manager_start_recording() == SD_OK) {
        // 录制帧数据
        sd_manager_write_frame(frame_data, frame_size);
        
        // 停止录制
        sd_manager_stop_recording();
    }
}
```

## 注意事项

1. 需要在项目中包含新的头文件路径
2. 编译时需要添加sd_manager.c到构建列表
3. 保持与原有sdcard_manager.h的兼容性
4. 文件索引管理现在由sd_manager内部处理