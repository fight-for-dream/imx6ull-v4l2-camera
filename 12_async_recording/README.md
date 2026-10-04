# 第 12 阶段：异步录像

第 11 阶段的 `RecordWriteFrame()` 直接调用 `fwrite()`。如果 eMMC 某次写入变慢，摄像头主循环也会停在这里，不能及时显示、发送 HTTP 和执行 `VIDIOC_QBUF`。

本阶段把工作拆成两个执行流程：

```text
摄像头主循环                       录像线程
DQBUF 取得 JPEG
复制 JPEG 到队列  ──────────────>  从队列取出 JPEG
立即 QBUF                          fwrite 写入 eMMC
继续采集下一帧                     释放这一帧的副本
```

必须复制 JPEG，因为 `QBUF` 以后原来的 V4L2 缓冲区重新属于驱动，录像线程不能继续访问它。

队列最多保存 8 帧。如果存储速度长期赶不上采集速度，队列满时丢弃当前录像帧，但摄像头、LCD 和 HTTP 继续运行。停止录像时会等待队列里已经接收的帧写完，然后执行 `fclose()`。主程序仍然只调用 `RecordToggle()`、`RecordWriteFrame()` 和 `RecordStop()`，线程、互斥锁、条件变量和队列状态全部封装在 `record.c`。

Ubuntu 没有 FFmpeg不影响本阶段编译和开发板测试。FFmpeg只在主机端播放或转换录像时才需要。

## 编译和上传

```sh
cd /home/book/hcm/hcm_v4l2/12_async_recording
make
adb shell mkdir -p /root/hcm/12_async_recording
adb push camera_async_recorder /root/hcm/12_async_recording/
```

## 开发板运行

```sh
cd /root/hcm/12_async_recording
./camera_async_recorder /dev/v4l/by-id/usb-lihappe8_Corp._USB_2.0_Camera-video-index0 /dev/fb0
```

- `r` 回车：开始或停止异步录像。
- `s` 回车：抓拍。
- `q` 回车：退出。

先录制约 10 秒，然后停止。预期输出类似：

```text
Recording stopped: 198 frames, 9200000 bytes, 0 dropped.
```

`dropped` 是因为录像队列已满而未写入文件的帧数。正常短录像应接近 0；它大于 0 说明写盘速度在某些时刻跟不上采集速度。

没有播放器时，至少可以检查文件首尾是否为完整 JPEG 标记：

```sh
ls -lh record_*.mjpg
od -An -tx1 -N2 record_XXXX_X.mjpg
tail -c 2 record_XXXX_X.mjpg | od -An -tx1
```

第一条 `od` 应输出 `ff d8`，最后一条应输出 `ff d9`。这只能检查原始流的首尾，不能代替实际播放验证。以后需要播放时，可在Ubuntu安装FFmpeg，或者把文件传到装有FFmpeg/VLC的Windows电脑。
