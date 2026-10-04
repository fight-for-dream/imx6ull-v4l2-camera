# 第八阶段：LCD 预览时抓拍 JPEG

本阶段保留第七阶段的实时预览，增加一个操作：在运行程序的终端输入 `s` 并回车，
保存当前帧；输入 `q` 并回车或按 `Ctrl+C` 退出。

相机本来就输出 MJPG，因此抓拍时直接写入当前帧的压缩字节，
不需要把 LCD 上的 RGB 重新压缩成 JPEG。保存动作必须在 `CameraPutFrame` 之前：
缓冲区还给驱动后，驱动可能立即写入下一帧。

文件分工：`camera.c` 取还帧，`display.c` 解码预览，`snapshot.c` 写 JPEG，
`main.c` 处理用户输入并调用它们。前三个文件中的 `camera`、`display` 逻辑沿用
已验证的第七阶段版本。

在 Ubuntu 中进入本目录执行 `make`。传到开发板后，在有交互输入的串口或
ADB 终端执行：

```sh
./camera_snapshot /dev/video1 /dev/fb0
```

抓拍文件保存在 `/tmp/camera_进程号_序号.jpg`，程序会打印实际完整路径。
`/tmp` 是临时文件系统，重启后文件会消失；需要保留时可从 Ubuntu 用
`adb pull <打印出的路径> <保存位置>` 复制出来。运行前停止其他绘制 `/dev/fb0` 的程序。
