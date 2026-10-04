# 第 10 阶段：摄像头拔出后的自动恢复

这一阶段保留第 9 阶段的 LCD 预览、JPEG 抓拍和 HTTP 视频查看，只增加摄像头断开后的恢复。程序不修改内核、设备树、启动方式或根文件系统。

## 实现思路

摄像头连接正常时，程序持续从 V4L2 取帧、显示、发布到 HTTP，再把缓冲区还给驱动。发生错误时，不把某个固定的 `errno` 当成唯一的“已拔出”标志：`poll` 错误、`VIDIOC_DQBUF` 失败、错误帧后 `VIDIOC_QBUF` 失败，都可能提示旧设备不可用。单次超时、`EAGAIN` 或一帧带 `V4L2_BUF_FLAG_ERROR` 也可能只是暂时现象，因此先跳过并重试；连续三次没有正常帧才重新打开。

进入恢复状态后，程序关闭**旧的** V4L2 文件描述符、解除旧缓冲区映射，每隔约 2 秒尝试从同一个摄像头路径重新执行初始化、申请缓冲区和 `STREAMON`。新插入的摄像头即使仍叫 `/dev/video1`，旧文件描述符也不能继续用。

LCD 和 HTTP 服务在摄像头断开期间不重建：LCD 保留最后画面，浏览器也停在最后一帧；重新得到新帧后两者继续更新。离线时 `q` 回车仍可退出，`s` 回车只提示无法抓拍。当前 HTTP 服务仍沿用第 9 阶段的单客户端设计；浏览器断开时应关闭旧页面后再打开新页面。

## 编译与运行

用 FileZilla 将本目录上传到 Ubuntu，例如 `/home/book/hcm/hcm_v4l2/10_camera_recovery`，在 Ubuntu 中执行：

```sh
cd /home/book/hcm/hcm_v4l2/10_camera_recovery
make
adb shell mkdir -p /root/hcm/10_camera_recovery
adb push camera_recovery /root/hcm/10_camera_recovery
```

在开发板上确认摄像头的稳定路径：

```sh
ls -l /dev/v4l/by-id/
```

按你之前实测的摄像头，运行命令为：

```sh
cd /root/hcm/10_camera_recovery
./camera_recovery /dev/v4l/by-id/usb-lihappe8_Corp._USB_2.0_Camera-video-index0 /dev/fb0
```

如果 `by-id` 名称与示例不同，以板上的 `ls` 结果为准。不要改用固定的 `/dev/video1` 作为正式测试路径，因为重新枚举后编号可能改变。Windows 有线网络已验证时，可以打开 `http://192.168.137.2:8080/stream`。

## 一次验证流程

1. 摄像头连接时启动程序，确认 LCD 和浏览器均有实时画面。
2. 从 USB 接口拔出摄像头一次，观察终端出现取帧失败、释放旧缓冲区及定时重试的信息；LCD/浏览器此时停在最后一帧。
3. 重新插入摄像头，观察终端出现 `Camera reconnected; preview resumed.`，确认 LCD 和浏览器恢复更新；再输入 `s` 回车验证能抓拍。
4. 输入 `q` 回车正常退出。把运行期间的终端输出，以及 `dmesg | tail -n 60` 发给我，一起判断是否有异常。

你之前的拔插测试在内核日志中出现过 `uvc_status_cleanup` 附近的 WARN。那属于内核侧现象，用户态重连代码不会修复它。因此先只做**一次**拔插验证，不要连续高频反复拔插。若板子出现死机、USB 无法重新枚举或其他不稳定情况，停止测试并保留日志。
