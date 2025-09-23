# Code 文件夹说明

本文件夹包含项目的所有自定义库和工具。

## 目录结构

### sd_manager/ - SD卡管理库

- **inc/sd_manager.h** - SD卡管理模块头文件
- **src/sd_manager.c** - SD卡管理模块实现
- **功能**：提供简化的SD卡录像API，包括BMP保存和原始数据录制

### docs/ - 文档

- **simplified_video_recording_api.md** - 简化录像API使用说明
- **sd_manager_encapsulation_summary.md** - SD卡模块封装总结

### examples/ - 示例代码

- 包含各种使用示例

### pc_tools/ - PC端工具

- 包含PC端辅助工具

## 使用说明

在主程序中包含SD管理库：

```c
#include "../../code/sd_manager/inc/sd_manager.h"
```

基本使用：

```c
// 初始化
sd_manager_init();

// 开始录像
sd_manager_start_video_recording(image_data, width, height);

// 录制帧
sd_manager_record_frame(image_data, width, height);

// 停止录像  
sd_manager_stop_video_recording();
```
