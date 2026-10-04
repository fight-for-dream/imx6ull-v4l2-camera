/*
 * Ubuntu 编译：make
 * 开发板运行：./v4l2_frame_interval /dev/video1
 *
 * 查询 MJPG 640x480 支持的帧间隔，不启动摄像头采集。
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
	struct v4l2_frmivalenum tFrmInterval;
	int iFd;
	int iRet;
	int iIndex;

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

	printf("MJPG 640x480 supported frame intervals:\n");
	iIndex = 0;
	while (1)
	{
		memset(&tFrmInterval, 0, sizeof(tFrmInterval));
		tFrmInterval.index = iIndex;
		tFrmInterval.pixel_format = V4L2_PIX_FMT_MJPEG;
		tFrmInterval.width = 640;
		tFrmInterval.height = 480;

		iRet = ioctl(iFd, VIDIOC_ENUM_FRAMEINTERVALS, &tFrmInterval);
		if (iRet < 0)
		{
			if (errno == EINVAL)
				break;
			perror("VIDIOC_ENUM_FRAMEINTERVALS");
			close(iFd);
			return 1;
		}

		if (tFrmInterval.type == V4L2_FRMIVAL_TYPE_DISCRETE)
		{
			if (tFrmInterval.discrete.numerator == 0 ||
			    tFrmInterval.discrete.denominator == 0)
			{
				fprintf(stderr, "Camera returned an invalid interval.\n");
				close(iFd);
				return 1;
			}
			printf("  %d: %u/%u second (%.2f fps)\n", iIndex,
			       tFrmInterval.discrete.numerator,
			       tFrmInterval.discrete.denominator,
			       (double)tFrmInterval.discrete.denominator /
			       tFrmInterval.discrete.numerator);
		}
		else if (tFrmInterval.type == V4L2_FRMIVAL_TYPE_CONTINUOUS ||
			 tFrmInterval.type == V4L2_FRMIVAL_TYPE_STEPWISE)
		{
			printf("  interval range: %u/%u to %u/%u second\n",
			       tFrmInterval.stepwise.min.numerator,
			       tFrmInterval.stepwise.min.denominator,
			       tFrmInterval.stepwise.max.numerator,
			       tFrmInterval.stepwise.max.denominator);
			iIndex++;
			break;
		}
		else
		{
			fprintf(stderr, "Unknown frame interval type: %u\n",
				tFrmInterval.type);
			close(iFd);
			return 1;
		}
		iIndex++;
	}

	if (iIndex == 0)
		printf("  camera did not report a frame interval\n");

	close(iFd);
	return 0;
}
