# 本阶段只编译用户态程序，不需要编译内核模块。
# 在 Ubuntu 中执行 make；交叉编译器需已加入 PATH。

CROSS_COMPILE = arm-buildroot-linux-gnueabihf-

all:
	$(CROSS_COMPILE)gcc -Wall -O2 -o v4l2_capture_one v4l2_capture_one.c

clean:
	rm -f v4l2_capture_one
