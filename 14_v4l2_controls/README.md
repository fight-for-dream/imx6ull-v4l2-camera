# 第14阶段：V4L2控制项枚举

本阶段学习V4L2的控制通路。第一步只读取摄像头控制项，不修改任何参数，也不修改第13阶段的综合程序。

程序依次完成：

```text
打开摄像头
    ↓
VIDIOC_QUERYCTRL枚举控制项
    ↓
VIDIOC_G_CTRL读取当前值
    ↓
VIDIOC_QUERYMENU枚举菜单含义
```

每个控制项会显示ID、名称、类型、最小值、最大值、步长、默认值、当前值和状态标志。程序优先使用`V4L2_CTRL_FLAG_NEXT_CTRL`遍历驱动实际注册的控制项；如果旧驱动不支持该方式，再扫描常见的用户类、摄像头类和私有控制ID范围。

## 编译和上传

用FileZilla把本目录上传到Ubuntu后执行：

```sh
cd /home/book/hcm/hcm_v4l2/14_v4l2_controls
make
adb shell mkdir -p /root/hcm/14_v4l2_controls
adb push v4l2_control /root/hcm/14_v4l2_controls/
```

## 开发板测试

先退出`camera_avi_recorder`，避免两个程序同时操作摄像头，然后执行：

```sh
cd /root/hcm/14_v4l2_controls
./v4l2_control /dev/v4l/by-id/usb-lihappe8_Corp._USB_2.0_Camera-video-index0 list
```

把完整输出发回来。下一步将根据真实结果选择适合实验的控制项，不会盲目假设摄像头一定支持亮度、曝光或白平衡。

当前版本故意不提供`set`命令；完成只读确认后，再增加范围检查、步长检查、写入和回读验证。
