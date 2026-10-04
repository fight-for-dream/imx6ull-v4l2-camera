# 第二阶段：采集一帧

当前开发板实测摄像头在 `/dev/video1`，驱动为 `uvcvideo`，支持 MJPG 和 YUYV，
支持 streaming，不支持普通 `read()`。本阶段选用 MJPG，直接把相机返回的一帧写入 JPEG 文件，
不做解码或图像转换。

在 Ubuntu 中进入本目录，使用本阶段的 Makefile 编译：

```sh
make
```

把生成的程序传到开发板后运行：

```sh
./v4l2_capture_one /dev/video1 /tmp/frame.jpg
```

程序请求 MJPG、640×480。`VIDIOC_S_FMT` 会返回摄像头实际采用的尺寸和格式；
如果返回的格式不是 MJPG，程序会停止，不把其他格式误存为 JPEG。
`/tmp/frame.jpg` 位于临时文件系统，开发板重启后会消失。

采集顺序：查询能力 → 设置格式 → 申请并映射缓冲区 → 入队 → 开始采集 →
等待并取出一帧 → 写文件 → 停止采集并释放资源。
