# 第 11 阶段：MJPEG 连续录像

本阶段在第 10 阶段基础上增加本地录像，并保留 LCD 预览、抓拍、HTTP 查看和摄像头重连。

摄像头输出的每一帧本来就是一张完整的 JPEG。录像时不再解码和编码，而是在归还 V4L2 缓冲区以前，把这些 JPEG 依次写入一个 `.mjpg` 文件：

```text
DQBUF 取出 JPEG
  ├─ 解码为 RGB，显示到 LCD
  ├─ 复制给 HTTP 服务
  └─ 原样写入录像文件
QBUF 归还缓冲区
```

这种文件是 MJPEG 原始视频流。它保存了连续图像，但没有 AVI/MP4 容器提供的时间轴和快进索引。它适合先学习录像数据通路；后续阶段再学习异步写盘和视频容器。

录像代码封装在 `record.c` 中。主流程只调用：

```c
RecordToggle();
RecordWriteFrame();
RecordStop();
```

为避免实验时忘记停止而占满开发板根文件系统，单个录像最多写入 100 MiB，到达上限会自动停止。

## 编译和上传

将本目录上传到 Ubuntu 的 `/home/book/hcm/hcm_v4l2/11_mjpeg_recording`，执行：

```sh
cd /home/book/hcm/hcm_v4l2/11_mjpeg_recording
make
adb shell mkdir -p /root/hcm/11_mjpeg_recording
adb push camera_recorder /root/hcm/11_mjpeg_recording/
```

## 开发板测试

```sh
cd /root/hcm/11_mjpeg_recording
./camera_recorder /dev/v4l/by-id/usb-lihappe8_Corp._USB_2.0_Camera-video-index0 /dev/fb0
```

终端命令：

- `r` 回车：开始录像；再次输入则停止录像。
- `s` 回车：保存一张 JPEG。
- `q` 回车：退出程序。

先录制约 5 秒，再按 `r` 停止。确认终端出现帧数和字节数，并检查文件：

```sh
ls -lh record_*.mjpg
```

把录像拉回 Ubuntu：

```sh
adb pull /root/hcm/11_mjpeg_recording/record_XXXX_X.mjpg ./
```

其中实际文件名以开发板上的 `ls` 输出为准。如果 Ubuntu 安装了 FFmpeg，可以按 20 fps 播放：

```sh
ffplay -f mjpeg -framerate 20 record_XXXX_X.mjpg
```

也可以无损封装成 AVI；`-c:v copy` 表示只添加容器，不重新编码 JPEG：

```sh
ffmpeg -f mjpeg -framerate 20 -i record_XXXX_X.mjpg -c:v copy record.avi
```

测试时同时确认 LCD 和浏览器画面仍正常。录像过程中如果拔出摄像头，程序会停止并关闭当前录像文件，等待摄像头重新连接。
