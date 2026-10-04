# 本阶段只编译用户态程序，不需要编译内核模块。
# 在 Ubuntu 中进入本目录执行 make。

CROSS_COMPILE = arm-buildroot-linux-gnueabihf-

all:
	$(CROSS_COMPILE)gcc -Wall -O2 -o v4l2_capture_fps v4l2_capture_fps.c

clean:
	rm -f v4l2_capture_fps
