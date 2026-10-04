/*
 * Ubuntu 编译：
 * arm-buildroot-linux-gnueabihf-gcc -Wall -O2 v4l2_capture_one.c -o v4l2_capture_one
 *
 * 开发板运行：
 * ./v4l2_capture_one /dev/video1 /tmp/frame.jpg
 */
#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#define VIDEO_BUFFER_COUNT 4
#define CAPTURE_TIMEOUT_MS 5000

struct video_buffer
{
	void *pucStart;
	size_t iLength;
};

int main(int argc, char *argv[])
{
	struct v4l2_capability tV4l2Cap;
	struct v4l2_format tV4l2Fmt;
	struct v4l2_requestbuffers tReqBuffs;
	struct v4l2_buffer tV4l2Buf;
	struct video_buffer tBuffers[VIDEO_BUFFER_COUNT];
	struct pollfd tPollFd;
	unsigned int uiCapabilities;
	FILE *pFile;
	size_t iWriteLen;
	int iFd;
	int iRet;
	int i;
	int iMappedCount;
	int iStreaming;
	int iResult;

	if (argc != 3)
	{
		printf("Usage: %s </dev/videoN> <output.jpg>\n", argv[0]);
		return 1;
	}

	iFd = -1;
	iMappedCount = 0;
	iStreaming = 0;
	iResult = 1;
	pFile = NULL;
	memset(tBuffers, 0, sizeof(tBuffers));

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

	/* 请求 MJPG 640x480；设备可能返回它实际支持的分辨率。 */
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

	printf("Actual format: %ux%u, %c%c%c%c\n",
	       tV4l2Fmt.fmt.pix.width, tV4l2Fmt.fmt.pix.height,
	       tV4l2Fmt.fmt.pix.pixelformat & 0xff,
	       (tV4l2Fmt.fmt.pix.pixelformat >> 8) & 0xff,
	       (tV4l2Fmt.fmt.pix.pixelformat >> 16) & 0xff,
	       (tV4l2Fmt.fmt.pix.pixelformat >> 24) & 0xff);
	if (tV4l2Fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_MJPEG)
	{
		fprintf(stderr, "Camera did not accept MJPG; refusing to write a JPEG file.\n");
		goto exit;
	}

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
	printf("Allocated buffers: %u\n", tReqBuffs.count);

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

	i = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	iRet = ioctl(iFd, VIDIOC_STREAMON, &i);
	if (iRet < 0)
	{
		perror("VIDIOC_STREAMON");
		goto exit;
	}
	iStreaming = 1;

	/* 等待一帧完成，O_NONBLOCK 下的 DQBUF 可能暂时返回 EAGAIN。 */
	while (1)
	{
		tPollFd.fd = iFd;
		tPollFd.events = POLLIN;
		iRet = poll(&tPollFd, 1, CAPTURE_TIMEOUT_MS);
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
			fprintf(stderr, "Video device reported an error while waiting.\n");
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
		break;
	}

	if (tV4l2Buf.index >= tReqBuffs.count ||
	    tV4l2Buf.bytesused == 0 ||
	    tV4l2Buf.bytesused > tBuffers[tV4l2Buf.index].iLength ||
	    (tV4l2Buf.flags & V4L2_BUF_FLAG_ERROR))
	{
		fprintf(stderr, "Camera returned an invalid frame.\n");
		goto exit;
	}

	pFile = fopen(argv[2], "wb");
	if (pFile == NULL)
	{
		perror("open output file");
		goto exit;
	}
	iWriteLen = fwrite(tBuffers[tV4l2Buf.index].pucStart, 1,
			   tV4l2Buf.bytesused, pFile);
	if (iWriteLen != tV4l2Buf.bytesused)
	{
		perror("write output file");
		goto exit;
	}
	if (fclose(pFile) != 0)
	{
		pFile = NULL;
		perror("close output file");
		goto exit;
	}
	pFile = NULL;

	printf("Saved one MJPG frame: %u bytes -> %s\n",
	       tV4l2Buf.bytesused, argv[2]);
	iResult = 0;

exit:
	if (pFile != NULL)
		fclose(pFile);
	if (iStreaming)
	{
		i = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		ioctl(iFd, VIDIOC_STREAMOFF, &i);
	}
	for (i = 0; i < iMappedCount; i++)
		munmap(tBuffers[i].pucStart, tBuffers[i].iLength);
	if (iFd >= 0)
		close(iFd);
	return iResult;
}
