# 第17阶段：浏览器控制页面

本阶段在第16阶段多客户端HTTP服务器上增加网页、状态查询、远程抓拍和远程录像切换。V4L2控制参数暂不加入网页，避免一次引入过多功能。

服务器提供：

```text
GET  /                    HTML控制页面
GET  /stream              MJPEG实时视频
GET  /api/status          摄像头与录像状态
POST /api/snapshot        提交抓拍命令
POST /api/record/toggle   提交录像切换命令
```

HTTP线程不会直接保存V4L2缓冲区，也不会直接启动或停止录像。它只在`web_command.c`中设置待处理标志；摄像头主线程取得有效JPEG后再执行命令：

```text
浏览器POST请求
    ↓
HTTP线程提交命令并返回202
    ↓
摄像头主线程处理下一帧
    ├─ 保存JPEG
    └─ 切换AVI录像
```

这样执行`QBUF`以后，HTTP线程不会继续访问已经归还驱动的V4L2缓冲区。终端和网页抓拍共用`snapshot.c`内部序号，避免文件名重复。

网页HTML直接编译进程序，不需要向根文件系统额外安装网页文件。当前页面面向可信局域网实验，没有身份认证和HTTPS，不能直接暴露到公网。

## 编译和上传

```sh
cd /home/book/hcm/hcm_v4l2/17_web_control
make
adb shell mkdir -p /root/hcm/17_web_control
adb push camera_web_control /root/hcm/17_web_control/
```

## 开发板运行

```sh
cd /root/hcm/17_web_control
./camera_web_control /dev/v4l/by-id/usb-lihappe8_Corp._USB_2.0_Camera-video-index0 /dev/fb0
```

Windows浏览器访问：

```text
http://192.168.137.2:8080/
```

如果使用无线网络，请替换为开发板当前无线IP。

## 测试顺序

1. 确认根路径能显示网页和实时视频。
2. 点击“抓拍”，开发板应打印保存到`/tmp/camera_...jpg`，网页的抓拍数量加1。
3. 点击“开始/停止录像”，第一次点击后网页显示“录像进行中”，再次点击后生成完整AVI并显示“未录像”。
4. 保持网页打开，在终端输入`s`和`r`，确认原有终端控制仍然有效。
5. 输入`q`退出，确认浏览器连接断开且程序不阻塞。

也可以直接检查API：

```powershell
curl.exe --noproxy "*" http://192.168.137.2:8080/api/status
curl.exe --noproxy "*" -X POST http://192.168.137.2:8080/api/snapshot
curl.exe --noproxy "*" -X POST http://192.168.137.2:8080/api/record/toggle
```
