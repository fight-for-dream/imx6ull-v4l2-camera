# 第15阶段：读取和设置V4L2控制项

第14阶段已经确认摄像头支持10个控制项。本阶段保留`list`命令，增加单项`get/set`，并在写入前校验范围、步长、菜单选项和状态标志，写入后立即回读确认。

```text
解析控制ID和目标值
    ↓
VIDIOC_QUERYCTRL重新查询当前状态
    ↓
检查范围、step、菜单、read-only和inactive
    ↓
VIDIOC_S_CTRL写入
    ↓
VIDIOC_G_CTRL回读
```

使用数字ID是为了直接理解V4L2 ABI。参数支持十六进制，例如`0x00980900`，也支持十进制。

## 编译和上传

```sh
cd /home/book/hcm/hcm_v4l2/15_v4l2_control_set
make
adb shell mkdir -p /root/hcm/15_v4l2_control_set
adb push v4l2_control /root/hcm/15_v4l2_control_set/
```

## 第一次测试：亮度

先退出摄像头综合程序，再执行：

```sh
cd /root/hcm/15_v4l2_control_set
./v4l2_control /dev/v4l/by-id/usb-lihappe8_Corp._USB_2.0_Camera-video-index0 get 0x00980900
./v4l2_control /dev/v4l/by-id/usb-lihappe8_Corp._USB_2.0_Camera-video-index0 set 0x00980900 50
./v4l2_control /dev/v4l/by-id/usb-lihappe8_Corp._USB_2.0_Camera-video-index0 get 0x00980900
./v4l2_control /dev/v4l/by-id/usb-lihappe8_Corp._USB_2.0_Camera-video-index0 set 0x00980900 0
```

最后一条命令把亮度恢复为默认值0。第一轮只测试亮度，不同时修改其他参数。

## 后续白平衡测试逻辑

摄像头当前开启自动白平衡，所以手动色温处于`inactive`。亮度测试成功后再按顺序验证：

```sh
# 关闭自动白平衡
./v4l2_control /dev/v4l/by-id/usb-lihappe8_Corp._USB_2.0_Camera-video-index0 set 0x0098090c 0

# 手动色温此时应恢复可用，再设置为4500
./v4l2_control /dev/v4l/by-id/usb-lihappe8_Corp._USB_2.0_Camera-video-index0 get 0x0098091a
./v4l2_control /dev/v4l/by-id/usb-lihappe8_Corp._USB_2.0_Camera-video-index0 set 0x0098091a 4500

# 恢复自动白平衡
./v4l2_control /dev/v4l/by-id/usb-lihappe8_Corp._USB_2.0_Camera-video-index0 set 0x0098090c 1
```

不要修改`Power Line Frequency`，当前值1已经代表50 Hz。Hue的步长是100，程序会拒绝不符合步长的数值。
