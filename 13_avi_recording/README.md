# 第 13 阶段：AVI 录像封装

本阶段把第12阶段生成的MJPEG原始流升级为可独立描述播放信息的AVI文件。摄像头仍然输出JPEG，程序不重新编码图像，只增加文件头、帧块和索引。

```text
RIFF AVI
├─ hdrl：MJPG、640×480、20 fps、总帧数
├─ movi：00dc帧块，每个块中是一帧完整JPEG
└─ idx1：每一帧在movi中的位置和长度
```

开始录像时总帧数和文件大小未知，所以先写0。停止录像时写入`idx1`索引，再通过`fseek()`回到文件头补写实际值。每帧数据按RIFF规则补齐到偶数字节边界。

AVI细节全部封装在`avi_writer.c`中，对录像线程只提供：

```c
AviWriterOpen();
AviWriterWriteFrame();
AviWriterClose();
```

第12阶段的8帧异步队列继续保留，主程序没有增加AVI变量。单个录像仍限制为大约100 MiB。本阶段使用传统AVI 1.0的`idx1`索引，适合当前短录像，不用于超过1 GiB的大文件。

## 编译和上传

```sh
cd /home/book/hcm/hcm_v4l2/13_avi_recording
make
adb shell mkdir -p /root/hcm/13_avi_recording
adb push camera_avi_recorder /root/hcm/13_avi_recording/
```

## 开发板运行

```sh
cd /root/hcm/13_avi_recording
./camera_avi_recorder /dev/v4l/by-id/usb-lihappe8_Corp._USB_2.0_Camera-video-index0 /dev/fb0
```

- `r`回车：开始或停止AVI录像。
- `s`回车：抓拍。
- `q`回车：退出。

录制约10秒后停止，检查：

```sh
ls -lh record_*.avi
```

然后拉回Ubuntu：

```sh
adb pull /root/hcm/13_avi_recording/record_XXXX_X.avi ./
```

Ubuntu不需要安装FFmpeg，也可以先识别文件结构：

```sh
file record_XXXX_X.avi
```

正常情况下会识别为RIFF/AVI、MJPEG、640×480。再通过FileZilla传到Windows，用VLC或支持MJPEG AVI的播放器打开。Windows自带播放器是否支持MJPEG取决于系统安装的解码器，因此不能打开时不代表文件一定损坏。

注意：必须正常按`r`停止录像或按`q`退出，程序才会写入索引并回填文件头。录像期间直接断电得到的AVI可能不完整，这会成为后续可靠性阶段需要解决的问题。
