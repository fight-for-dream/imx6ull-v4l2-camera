# 本阶段编译用户态 LCD 预览、抓拍和网络视频程序。
# 在 Ubuntu 中进入本目录执行 make。

CROSS_COMPILE = arm-buildroot-linux-gnueabihf-

all:
	$(CROSS_COMPILE)gcc -Wall -O2 -pthread -o camera_mjpeg_server main.c camera.c display.c snapshot.c http_server.c -ljpeg

clean:
	rm -f camera_mjpeg_server
