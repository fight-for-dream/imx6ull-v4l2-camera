/*
 * Ubuntu 编译：
 * arm-buildroot-linux-gnueabihf-gcc -Wall -O2 v4l2_probe.c -o v4l2_probe
 *
 * 开发板运行：
 * ./v4l2_probe /dev/video0
 */
#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
	struct v4l2_capability tV4l2Cap;
	struct v4l2_fmtdesc tFmtDesc;
	const char *strDevName;
	unsigned int uiCapabilities;
	int iFd;
	int iRet;
	int iIndex;
	int iBufType;

	if (argc > 2)
	{
		printf("Usage: %s [/dev/videoN]\n", argv[0]);
		return 1;
	}

	strDevName = (argc == 2) ? argv[1] : "/dev/video1";
	iFd = open(strDevName, O_RDWR);
	if (iFd < 0)
	{
		perror("open video device");
		return 1;
	}

	memset(&tV4l2Cap, 0, sizeof(tV4l2Cap));
	iRet = ioctl(iFd, VIDIOC_QUERYCAP, &tV4l2Cap);
	if (iRet < 0)
	{
		perror("VIDIOC_QUERYCAP");
		goto err_exit;
	}

	if (tV4l2Cap.capabilities & V4L2_CAP_DEVICE_CAPS)
		uiCapabilities = tV4l2Cap.device_caps;
	else
		uiCapabilities = tV4l2Cap.capabilities;

	printf("device: %s\n", strDevName);
	printf("driver: %.*s\n", (int)sizeof(tV4l2Cap.driver),
	       (const char *)tV4l2Cap.driver);
	printf("card: %.*s\n", (int)sizeof(tV4l2Cap.card),
	       (const char *)tV4l2Cap.card);
	printf("bus: %.*s\n", (int)sizeof(tV4l2Cap.bus_info),
	       (const char *)tV4l2Cap.bus_info);
	printf("capture: %s\n",
	       (uiCapabilities & (V4L2_CAP_VIDEO_CAPTURE |
				  V4L2_CAP_VIDEO_CAPTURE_MPLANE)) ? "yes" : "no");
	printf("streaming: %s\n",
	       (uiCapabilities & V4L2_CAP_STREAMING) ? "yes" : "no");
	printf("read: %s\n",
	       (uiCapabilities & V4L2_CAP_READWRITE) ? "yes" : "no");

	if (uiCapabilities & V4L2_CAP_VIDEO_CAPTURE)
		iBufType = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	else if (uiCapabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE)
		iBufType = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	else
	{
		printf("This device does not support video capture.\n");
		goto err_exit;
	}

	printf("supported formats:\n");
	iIndex = 0;
	while (1)
	{
		memset(&tFmtDesc, 0, sizeof(tFmtDesc));
		tFmtDesc.index = iIndex;
		tFmtDesc.type = iBufType;
		iRet = ioctl(iFd, VIDIOC_ENUM_FMT, &tFmtDesc);
		if (iRet < 0)
		{
			if (errno != EINVAL)
			{
				perror("VIDIOC_ENUM_FMT");
				goto err_exit;
			}
			break;
		}

		printf("  %d: %c%c%c%c (%.*s)\n", iIndex,
		       tFmtDesc.pixelformat & 0xff,
		       (tFmtDesc.pixelformat >> 8) & 0xff,
		       (tFmtDesc.pixelformat >> 16) & 0xff,
		       (tFmtDesc.pixelformat >> 24) & 0xff,
		       (int)sizeof(tFmtDesc.description),
		       (const char *)tFmtDesc.description);
		iIndex++;
	}

	if (iIndex == 0)
		printf("  no format reported\n");

	close(iFd);
	return 0;

err_exit:
	close(iFd);
	return 1;
}
