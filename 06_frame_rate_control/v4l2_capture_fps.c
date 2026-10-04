/*
 * Ubuntu 编译：make
 * 开发板运行：./v4l2_capture_fps /dev/video1 20
 *
 * 在同一次采集中设置帧率，并测量实际接收速率。
 */
#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define VIDEO_BUFFER_COUNT 4
#define FRAME_COUNT 100
#define WAIT_TIMEOUT_MS 5000

struct video_buffer
{
	void *pucStart;
	size_t iLength;
};

int main(int argc, char *argv[])
{
	struct v4l2_capability tV4l2Cap;
	struct v4l2_format tV4l2Fmt;
	struct v4l2_streamparm tStreamParm;
	struct v4l2_requestbuffers tReqBuffs;
	struct v4l2_buffer tV4l2Buf;
	struct video_buffer tBuffers[VIDEO_BUFFER_COUNT];
	struct pollfd tPollFd;
	struct timespec tStart;
	struct timespec tFirstFrame;
	struct timespec tEnd;
	unsigned int uiCapabilities;
	double dSeconds;
	double dSteadySeconds;
	unsigned int uiLastSequence;
	unsigned int uiDroppedFrames;
	int iFd;
	int iRet;
	int i;
	int iType;
	int iBufIndex;
	int iMappedCount;
	int iStreaming;
	int iFrameCount;
	int iRequestedFps;
	int iHasSequence;
	int iResult;

	if (argc != 3 ||
	    (strcmp(argv[2], "20") != 0 && strcmp(argv[2], "30") != 0))
	{
		printf("Usage: %s </dev/videoN> <20|30>\n", argv[0]);
		return 1;
	}
	iRequestedFps = atoi(argv[2]);

	iFd = -1;
	iMappedCount = 0;
	iStreaming = 0;
	iFrameCount = 0;
	iHasSequence = 0;
	uiLastSequence = 0;
	uiDroppedFrames = 0;
	iResult = 1;
	memset(tBuffers, 0, sizeof(tBuffers));

	/* 1. 打开设备，确认它支持视频采集和 streaming。 */
	iFd = open(argv[1], O_RDWR | O_NONBLOCK);
	if (iFd < 0)
	{
		perror("open video device");
		goto exit;
	}

	memset(&tV4l2Cap, 0, sizeof(tV4l2Cap));
	iRet = ioctl(iFd, VIDIOC_QUERYCAP, &tV4l2Cap);
	if (iRet < 0)
	{
		perror("VIDIOC_QUERYCAP");
		goto exit;
	}
	if (tV4l2Cap.capabilities & V4L2_CAP_DEVICE_CAPS)
		uiCapabilities = tV4l2Cap.device_caps;
	else
		uiCapabilities = tV4l2Cap.capabilities;
	if (!(uiCapabilities & V4L2_CAP_VIDEO_CAPTURE) ||
	    !(uiCapabilities & V4L2_CAP_STREAMING))
	{
		fprintf(stderr, "Device does not support video capture with streaming.\n");
		goto exit;
	}

	/* 2. 请求 MJPG 640x480，以驱动返回的实际参数为准。 */
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
		goto exit;
	}
	if (tV4l2Fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_MJPEG)
	{
		fprintf(stderr, "Camera did not accept MJPG.\n");
		goto exit;
	}
	printf("Actual format: %ux%u MJPG\n",
	       tV4l2Fmt.fmt.pix.width, tV4l2Fmt.fmt.pix.height);

	/* 3. 在申请缓冲区前设置帧间隔：1/20 或 1/30 秒。 */
	memset(&tStreamParm, 0, sizeof(tStreamParm));
	tStreamParm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	tStreamParm.parm.capture.timeperframe.numerator = 1;
	tStreamParm.parm.capture.timeperframe.denominator = iRequestedFps;
	iRet = ioctl(iFd, VIDIOC_S_PARM, &tStreamParm);
	if (iRet < 0)
	{
		perror("VIDIOC_S_PARM");
		goto exit;
	}
	if (tStreamParm.parm.capture.timeperframe.numerator == 0 ||
	    tStreamParm.parm.capture.timeperframe.denominator == 0)
	{
		fprintf(stderr, "Camera returned an invalid frame interval.\n");
		goto exit;
	}
	printf("Requested: %d fps; driver accepted: %u/%u second (%.2f fps)\n",
	       iRequestedFps,
	       tStreamParm.parm.capture.timeperframe.numerator,
	       tStreamParm.parm.capture.timeperframe.denominator,
	       (double)tStreamParm.parm.capture.timeperframe.denominator /
	       tStreamParm.parm.capture.timeperframe.numerator);

	/* 4. 申请缓冲区，并映射到本程序的地址空间。 */
	memset(&tReqBuffs, 0, sizeof(tReqBuffs));
	tReqBuffs.count = VIDEO_BUFFER_COUNT;
	tReqBuffs.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	tReqBuffs.memory = V4L2_MEMORY_MMAP;
	iRet = ioctl(iFd, VIDIOC_REQBUFS, &tReqBuffs);
	if (iRet < 0)
	{
		perror("VIDIOC_REQBUFS");
		goto exit;
	}
	if (tReqBuffs.count == 0 || tReqBuffs.count > VIDEO_BUFFER_COUNT)
	{
		fprintf(stderr, "Unexpected buffer count: %u\n", tReqBuffs.count);
		goto exit;
	}

	for (i = 0; i < (int)tReqBuffs.count; i++)
	{
		memset(&tV4l2Buf, 0, sizeof(tV4l2Buf));
		tV4l2Buf.index = i;
		tV4l2Buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		tV4l2Buf.memory = V4L2_MEMORY_MMAP;
		iRet = ioctl(iFd, VIDIOC_QUERYBUF, &tV4l2Buf);
		if (iRet < 0)
		{
			perror("VIDIOC_QUERYBUF");
			goto exit;
		}

		tBuffers[i].iLength = tV4l2Buf.length;
		tBuffers[i].pucStart = mmap(NULL, tV4l2Buf.length,
					    PROT_READ | PROT_WRITE, MAP_SHARED,
					    iFd, tV4l2Buf.m.offset);
		if (tBuffers[i].pucStart == MAP_FAILED)
		{
			perror("mmap");
			goto exit;
		}
		iMappedCount++;
	}

	/* 5. 把空缓冲区交给驱动，然后启动摄像头。 */
	for (i = 0; i < (int)tReqBuffs.count; i++)
	{
		memset(&tV4l2Buf, 0, sizeof(tV4l2Buf));
		tV4l2Buf.index = i;
		tV4l2Buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		tV4l2Buf.memory = V4L2_MEMORY_MMAP;
		iRet = ioctl(iFd, VIDIOC_QBUF, &tV4l2Buf);
		if (iRet < 0)
		{
			perror("VIDIOC_QBUF");
			goto exit;
		}
	}

	iType = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	iRet = ioctl(iFd, VIDIOC_STREAMON, &iType);
	if (iRet < 0)
	{
		perror("VIDIOC_STREAMON");
		goto exit;
	}
	iStreaming = 1;
	if (clock_gettime(CLOCK_MONOTONIC, &tStart) < 0)
	{
		perror("clock_gettime");
		goto exit;
	}

	/* 6. 等待一帧、取出缓冲区、再把它放回驱动。 */
	while (iFrameCount < FRAME_COUNT)
	{
		tPollFd.fd = iFd;
		tPollFd.events = POLLIN;
		iRet = poll(&tPollFd, 1, WAIT_TIMEOUT_MS);
		if (iRet < 0 && errno == EINTR)
			continue;
		if (iRet < 0)
		{
			perror("poll");
			goto exit;
		}
		if (iRet == 0)
		{
			fprintf(stderr, "Timed out waiting for a frame.\n");
			goto exit;
		}
		if (tPollFd.revents & (POLLERR | POLLHUP | POLLNVAL))
		{
			fprintf(stderr, "Video device reported an error.\n");
			goto exit;
		}

		memset(&tV4l2Buf, 0, sizeof(tV4l2Buf));
		tV4l2Buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		tV4l2Buf.memory = V4L2_MEMORY_MMAP;
		iRet = ioctl(iFd, VIDIOC_DQBUF, &tV4l2Buf);
		if (iRet < 0 && errno == EAGAIN)
			continue;
		if (iRet < 0)
		{
			perror("VIDIOC_DQBUF");
			goto exit;
		}
		if (tV4l2Buf.index >= tReqBuffs.count ||
		    tV4l2Buf.bytesused == 0 ||
		    tV4l2Buf.bytesused > tBuffers[tV4l2Buf.index].iLength ||
		    (tV4l2Buf.flags & V4L2_BUF_FLAG_ERROR))
		{
			fprintf(stderr, "Camera returned an invalid frame.\n");
			goto exit;
		}

		/* 第一帧的等待时间只计入 total，不计入 steady。 */
		if (iFrameCount == 0 &&
		    clock_gettime(CLOCK_MONOTONIC, &tFirstFrame) < 0)
		{
			perror("clock_gettime");
			goto exit;
		}
		if (iHasSequence && tV4l2Buf.sequence > uiLastSequence + 1)
			uiDroppedFrames += tV4l2Buf.sequence - uiLastSequence - 1;
		uiLastSequence = tV4l2Buf.sequence;
		iHasSequence = 1;
		iFrameCount++;
		if (iFrameCount == 1 || iFrameCount % 20 == 0)
			printf("Frame %d: %u bytes, sequence %u\n",
			       iFrameCount, tV4l2Buf.bytesused, tV4l2Buf.sequence);

		/* 这一帧已处理完，按 index 把同一个缓冲区重新入队。 */
		iBufIndex = tV4l2Buf.index;
		memset(&tV4l2Buf, 0, sizeof(tV4l2Buf));
		tV4l2Buf.index = iBufIndex;
		tV4l2Buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		tV4l2Buf.memory = V4L2_MEMORY_MMAP;
		iRet = ioctl(iFd, VIDIOC_QBUF, &tV4l2Buf);
		if (iRet < 0)
		{
			perror("VIDIOC_QBUF");
			goto exit;
		}
	}

	if (clock_gettime(CLOCK_MONOTONIC, &tEnd) < 0)
	{
		perror("clock_gettime");
		goto exit;
	}
	dSeconds = (double)(tEnd.tv_sec - tStart.tv_sec) +
		   (double)(tEnd.tv_nsec - tStart.tv_nsec) / 1000000000.0;
	dSteadySeconds = (double)(tEnd.tv_sec - tFirstFrame.tv_sec) +
			 (double)(tEnd.tv_nsec - tFirstFrame.tv_nsec) / 1000000000.0;
	printf("Captured %d frames in %.2f s", iFrameCount, dSeconds);
	if (dSeconds > 0.0)
		printf(", total %.2f fps", iFrameCount / dSeconds);
	printf("\n");
	if (dSteadySeconds > 0.0)
		printf("Between first and last frame: %.2f fps\n",
		       (iFrameCount - 1) / dSteadySeconds);
	printf("Sequence gaps reported by driver: %u\n", uiDroppedFrames);
	iResult = 0;

exit:
	if (iStreaming)
	{
		iType = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		ioctl(iFd, VIDIOC_STREAMOFF, &iType);
	}
	for (i = 0; i < iMappedCount; i++)
		munmap(tBuffers[i].pucStart, tBuffers[i].iLength);
	if (iFd >= 0)
		close(iFd);
	return iResult;
}
