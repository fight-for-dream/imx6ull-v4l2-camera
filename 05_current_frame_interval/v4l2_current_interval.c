/*
 * Ubuntu 编译：make
 * 开发板运行：./v4l2_current_interval /dev/video1
 *
 * 设置为 MJPG 640x480 后，查询当前采用的帧间隔。
 */
#include <fcntl.h>
#include <linux/videodev2.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
	struct v4l2_format tV4l2Fmt;
	struct v4l2_streamparm tStreamParm;
	int iFd;
	int iRet;

	if (argc != 2)
	{
		printf("Usage: %s </dev/videoN>\n", argv[0]);
		return 1;
	}

	iFd = open(argv[1], O_RDWR);
	if (iFd < 0)
	{
		perror("open video device");
		return 1;
	}

	/* 和第三阶段一样，请求 MJPG 640x480。 */
	memset(&tV4l2Fmt, 0, sizeof(tV4l2Fmt));
	tV4l2Fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	tV4l2Fmt.fmt.pix.width = 640;
	tV4l2Fmt.fmt.pix.height = 480;
	tV4l2Fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
	tV4l2Fmt.fmt.pix.field = V4L2_FIELD_ANY;
	iRet = ioctl(iFd, VIDIOC_S_FMT, &tV4l2Fmt);
	if (iRet < 0)
	{
		perror("VIDIOC_S_FMT");
		close(iFd);
		return 1;
	}
	if (tV4l2Fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_MJPEG)
	{
		fprintf(stderr, "Camera did not accept MJPG.\n");
		close(iFd);
		return 1;
	}
	printf("Actual format: %ux%u MJPG\n",
	       tV4l2Fmt.fmt.pix.width, tV4l2Fmt.fmt.pix.height);

	/* G_PARM 查询当前设置；上一阶段的 ENUM 查询所有可选档位。 */
	memset(&tStreamParm, 0, sizeof(tStreamParm));
	tStreamParm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	iRet = ioctl(iFd, VIDIOC_G_PARM, &tStreamParm);
	if (iRet < 0)
	{
		perror("VIDIOC_G_PARM");
		close(iFd);
		return 1;
	}

	if (!(tStreamParm.parm.capture.capability & V4L2_CAP_TIMEPERFRAME) ||
	    tStreamParm.parm.capture.timeperframe.numerator == 0 ||
	    tStreamParm.parm.capture.timeperframe.denominator == 0)
	{
		printf("Camera did not report a usable current frame interval.\n");
	}
	else
	{
		printf("Current interval: %u/%u second (%.2f fps)\n",
		       tStreamParm.parm.capture.timeperframe.numerator,
		       tStreamParm.parm.capture.timeperframe.denominator,
		       (double)tStreamParm.parm.capture.timeperframe.denominator /
		       tStreamParm.parm.capture.timeperframe.numerator);
	}

	close(iFd);
	return 0;
}
