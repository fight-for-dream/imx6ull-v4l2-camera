# 本阶段编译用户态摄像头预览程序，不编译内核模块。
# 在 Ubuntu 中进入本目录执行 make。

CROSS_COMPILE = arm-buildroot-linux-gnueabihf-

all:
	$(CROSS_COMPILE)gcc -Wall -O2 -o camera_preview main.c camera.c display.c -ljpeg

clean:
	rm -f camera_preview
