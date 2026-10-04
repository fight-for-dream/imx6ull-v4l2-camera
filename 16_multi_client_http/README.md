# 第16阶段：多客户端HTTP视频服务

第13阶段的HTTP线程在接收一个`/stream`客户端后，会一直为它发送视频，只有该客户端断开后才继续`accept()`。本阶段把监听与客户端处理分开，最多允许4个客户端同时连接。

```text
accept监听线程
  ├─ 客户端线程1：/stream
  ├─ 客户端线程2：/stream
  ├─ 客户端线程3：后续控制请求
  └─ 客户端线程4：后续控制请求
```

摄像头主线程仍然只保存一份最新JPEG。发布新帧时使用`pthread_cond_broadcast()`唤醒所有视频客户端；每个客户端保存自己的已发送帧序号和发送缓冲区。慢客户端只会跳过旧帧，发送超时后单独断开，不拖慢其他客户端和摄像头。

客户端数组、线程和socket全部封装在`http_server.c`中，`main.c`没有增加网络变量。超过4个客户端时，服务器返回`503 Service Unavailable`，避免i.MX6ULL资源被无限连接耗尽。

当前仍然只支持`/stream`；根路径`/`继续返回404。第17阶段才增加控制页面和API。

## 编译和上传

```sh
cd /home/book/hcm/hcm_v4l2/16_multi_client_http
make
adb shell mkdir -p /root/hcm/16_multi_client_http
adb push camera_multi_http /root/hcm/16_multi_client_http/
```

## 开发板运行

```sh
cd /root/hcm/16_multi_client_http
./camera_multi_http /dev/v4l/by-id/usb-lihappe8_Corp._USB_2.0_Camera-video-index0 /dev/fb0
```

## 测试逻辑

1. Windows浏览器打开`http://192.168.137.2:8080/stream`并保持播放。
2. 在第二个浏览器或另一台电脑打开同一地址，确认两个画面同时更新。
3. 关闭其中一个页面，确认另一个页面仍持续更新。
4. 保持浏览器播放，同时在Windows PowerShell执行：

```powershell
curl.exe --noproxy "*" --max-time 5 http://192.168.137.2:8080/stream -o NUL
```

`curl`在5秒后超时属于正常现象；这段时间浏览器画面应保持更新。开发板日志应同时出现两个或更多活动客户端。

如果使用无线地址，请把`192.168.137.2`替换为开发板当前无线IP。
