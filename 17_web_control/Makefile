# 在Ubuntu中进入本目录执行make。
CROSS_COMPILE = arm-buildroot-linux-gnueabihf-

all:
	$(CROSS_COMPILE)gcc -Wall -O2 -pthread -o camera_web_control main.c camera.c display.c snapshot.c http_server.c web_command.c record.c avi_writer.c -ljpeg

clean:
	rm -f camera_web_control
