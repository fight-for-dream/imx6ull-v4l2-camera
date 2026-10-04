# 第18阶段：网页摄像头参数控制

本阶段在第17阶段网页中增加亮度、自动白平衡和手动色温控制。只开放已经在第15阶段验证成功的参数，不把摄像头全部控制项一次堆进页面。

```text
GET  /api/controls                      读取缓存的实际参数
POST /api/control?id=<ID>&value=<值>    提交一条参数命令
```

HTTP线程只解析ID和值并设置`control pending`。摄像头主线程处理下一帧时，调用`camera_control.c`完成：

```text
VIDIOC_QUERYCTRL
    ↓ 检查范围、step、read-only、inactive
VIDIOC_S_CTRL
    ↓
VIDIOC_G_CTRL回读
    ↓
重新读取亮度和白平衡状态，供网页显示
```

网页滑块使用`change`事件，只在松开滑块后发送最终值，不在拖动过程中持续创建HTTP请求。自动白平衡开启时，网页禁用手动色温滑块；服务端仍会独立检查`inactive`，不依赖前端保证正确性。

控制命令仍通过`web_command.c`交给摄像头主线程，因此HTTP线程不直接操作摄像头文件描述符。一次只允许一条参数命令等待执行，上一条尚未完成时返回HTTP 409。

## 编译和上传

```sh
cd /home/book/hcm/hcm_v4l2/18_web_camera_controls
make
adb shell mkdir -p /root/hcm/18_web_camera_controls
adb push camera_web_controls /root/hcm/18_web_camera_controls/
```

## 开发板运行

```sh
cd /root/hcm/18_web_camera_controls
./camera_web_controls /dev/v4l/by-id/usb-lihappe8_Corp._USB_2.0_Camera-video-index0 /dev/fb0
```

Windows访问：

```text
http://192.168.137.2:8080/
```

## 测试顺序

1. 确认网页显示亮度0、自动白平衡开启、手动色温滑块不可用。
2. 把亮度改为50，松开滑块；确认提示中出现`requested 50, actual 50`，LCD和网页画面亮度发生变化。
3. 把亮度恢复为0。
4. 关闭自动白平衡，等待页面刷新，确认手动色温滑块恢复可用。
5. 把色温从5000改为4500，再恢复5000。
6. 重新开启自动白平衡，确认手动色温滑块再次不可用。
7. 同时复测抓拍、AVI录像和终端`s/r/q`命令。

测试期间不要同时运行第15阶段的`v4l2_control`工具，避免两个进程同时修改同一摄像头参数。

也可以直接查看API：

```powershell
curl.exe --noproxy "*" http://192.168.137.2:8080/api/controls
curl.exe --noproxy "*" -X POST "http://192.168.137.2:8080/api/control?id=0x00980900&value=50"
```
