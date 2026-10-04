#include "camera.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#define WAIT_TIMEOUT_MS 5000

int CameraInitDevice(struct camera_device *ptCamera, const char *strDevName)
{
	struct v4l2_capability tCap;
	struct v4l2_format tFmt;
	struct v4l2_streamparm tParm;
	struct v4l2_requestbuffers tReq;
	struct v4l2_buffer tBuf;
	unsigned int iCapabilities;
	int iType;
	int iRet;
	int i;

	memset(ptCamera, 0, sizeof(*ptCamera));
	ptCamera->iFd = -1;
	ptCamera->iFd = open(strDevName, O_RDWR | O_NONBLOCK);
	if (ptCamera->iFd < 0)
	{
		perror("open camera");
		return -1;
	}

	/* 1. 确认摄像头支持视频采集和 streaming。 */
	memset(&tCap, 0, sizeof(tCap));
	iRet = ioctl(ptCamera->iFd, VIDIOC_QUERYCAP, &tCap);
	if (iRet < 0)
	{
		perror("VIDIOC_QUERYCAP");
		goto err_exit;
	}
	iCapabilities = (tCap.capabilities & V4L2_CAP_DEVICE_CAPS) ?
		tCap.device_caps : tCap.capabilities;
	if (!(iCapabilities & V4L2_CAP_VIDEO_CAPTURE) ||
	    !(iCapabilities & V4L2_CAP_STREAMING))
	{
		fprintf(stderr, "Camera does not support streaming capture.\n");
		goto err_exit;
	}

	/* 2. MJPG 帧后续既能解码到 LCD，也能用于抓拍和网络传输。 */
	memset(&tFmt, 0, sizeof(tFmt));
	tFmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	tFmt.fmt.pix.width = 640;
	tFmt.fmt.pix.height = 480;
	tFmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
	tFmt.fmt.pix.field = V4L2_FIELD_ANY;
	iRet = ioctl(ptCamera->iFd, VIDIOC_S_FMT, &tFmt);
	if (iRet < 0)
	{
		perror("VIDIOC_S_FMT");
		goto err_exit;
	}
	if (tFmt.fmt.pix.pixelformat != V4L2_PIX_FMT_MJPEG)
	{
		fprintf(stderr, "Camera did not accept MJPG.\n");
		goto err_exit;
	}
	printf("Camera: %ux%u MJPG\n", tFmt.fmt.pix.width, tFmt.fmt.pix.height);

	/* 首版请求已验证能稳定采集的 20 fps。 */
	memset(&tParm, 0, sizeof(tParm));
	tParm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	tParm.parm.capture.timeperframe.numerator = 1;
	tParm.parm.capture.timeperframe.denominator = 20;
	iRet = ioctl(ptCamera->iFd, VIDIOC_S_PARM, &tParm);
	if (iRet < 0)
	{
		perror("VIDIOC_S_PARM");
		goto err_exit;
	}
	if (tParm.parm.capture.timeperframe.numerator == 0 ||
	    tParm.parm.capture.timeperframe.denominator == 0)
	{
		fprintf(stderr, "Camera returned an invalid frame interval.\n");
		goto err_exit;
	}
	printf("Camera interval: %u/%u second\n",
	       tParm.parm.capture.timeperframe.numerator,
	       tParm.parm.capture.timeperframe.denominator);

	/* 3. 申请并映射 4 个缓冲区。 */
	memset(&tReq, 0, sizeof(tReq));
	tReq.count = CAMERA_BUFFER_COUNT;
	tReq.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	tReq.memory = V4L2_MEMORY_MMAP;
	iRet = ioctl(ptCamera->iFd, VIDIOC_REQBUFS, &tReq);
	if (iRet < 0)
	{
		perror("VIDIOC_REQBUFS");
		goto err_exit;
	}
	if (tReq.count == 0 || tReq.count > CAMERA_BUFFER_COUNT)
	{
		fprintf(stderr, "Unexpected buffer count: %u\n", tReq.count);
		goto err_exit;
	}
	ptCamera->iBufferCount = tReq.count;

	for (i = 0; i < (int)ptCamera->iBufferCount; i++)
	{
		memset(&tBuf, 0, sizeof(tBuf));
		tBuf.index = i;
		tBuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		tBuf.memory = V4L2_MEMORY_MMAP;
		iRet = ioctl(ptCamera->iFd, VIDIOC_QUERYBUF, &tBuf);
		if (iRet < 0)
		{
			perror("VIDIOC_QUERYBUF");
			goto err_exit;
		}
		ptCamera->tBuffers[i].iLength = tBuf.length;
		ptCamera->tBuffers[i].pStart = mmap(NULL, tBuf.length,
						 PROT_READ | PROT_WRITE, MAP_SHARED,
						 ptCamera->iFd, tBuf.m.offset);
		if (ptCamera->tBuffers[i].pStart == MAP_FAILED)
		{
			perror("mmap camera buffer");
			goto err_exit;
		}
		ptCamera->iMappedCount++;
	}

	/* 4. 交出空缓冲区，开始采集。 */
	for (i = 0; i < (int)ptCamera->iBufferCount; i++)
	{
		memset(&tBuf, 0, sizeof(tBuf));
		tBuf.index = i;
		tBuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		tBuf.memory = V4L2_MEMORY_MMAP;
		iRet = ioctl(ptCamera->iFd, VIDIOC_QBUF, &tBuf);
		if (iRet < 0)
		{
			perror("VIDIOC_QBUF");
			goto err_exit;
		}
	}
	iType = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	iRet = ioctl(ptCamera->iFd, VIDIOC_STREAMON, &iType);
	if (iRet < 0)
	{
		perror("VIDIOC_STREAMON");
		goto err_exit;
	}
	ptCamera->iStreaming = 1;
	return 0;

err_exit:
	CameraExitDevice(ptCamera);
	return -1;
}

int CameraGetFrame(struct camera_device *ptCamera, struct camera_frame *ptFrame)
{
	struct pollfd tPollFd;
	struct v4l2_buffer tBuf;
	int iRet;

	while (1)
	{
		tPollFd.fd = ptCamera->iFd;
		tPollFd.events = POLLIN;
		iRet = poll(&tPollFd, 1, WAIT_TIMEOUT_MS);
		if (iRet < 0 && errno == EINTR)
			continue;
		if (iRet <= 0)
		{
			if (iRet == 0)
				fprintf(stderr, "Camera frame timeout.\n");
			else
				perror("poll camera");
			return -1;
		}
		if (tPollFd.revents & (POLLERR | POLLHUP | POLLNVAL))
		{
			fprintf(stderr, "Camera reported a poll error.\n");
			return -1;
		}

		memset(&tBuf, 0, sizeof(tBuf));
		tBuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		tBuf.memory = V4L2_MEMORY_MMAP;
		iRet = ioctl(ptCamera->iFd, VIDIOC_DQBUF, &tBuf);
		if (iRet < 0 && errno == EAGAIN)
			continue;
		if (iRet < 0)
		{
			perror("VIDIOC_DQBUF");
			return -1;
		}
		if (tBuf.index >= ptCamera->iBufferCount ||
		    tBuf.bytesused == 0 ||
		    tBuf.bytesused > ptCamera->tBuffers[tBuf.index].iLength ||
		    (tBuf.flags & V4L2_BUF_FLAG_ERROR))
		{
			fprintf(stderr, "Camera returned an invalid frame.\n");
			return -1;
		}

		ptFrame->pData = ptCamera->tBuffers[tBuf.index].pStart;
		ptFrame->iLength = tBuf.bytesused;
		ptFrame->iIndex = tBuf.index;
		ptFrame->iSequence = tBuf.sequence;
		return 0;
	}
}

int CameraPutFrame(struct camera_device *ptCamera, const struct camera_frame *ptFrame)
{
	struct v4l2_buffer tBuf;

	memset(&tBuf, 0, sizeof(tBuf));
	tBuf.index = ptFrame->iIndex;
	tBuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	tBuf.memory = V4L2_MEMORY_MMAP;
	if (ioctl(ptCamera->iFd, VIDIOC_QBUF, &tBuf) < 0)
	{
		perror("VIDIOC_QBUF");
		return -1;
	}
	return 0;
}

void CameraExitDevice(struct camera_device *ptCamera)
{
	int iType;
	int i;

	if (ptCamera->iStreaming)
	{
		iType = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		ioctl(ptCamera->iFd, VIDIOC_STREAMOFF, &iType);
	}
	for (i = 0; i < ptCamera->iMappedCount; i++)
		munmap(ptCamera->tBuffers[i].pStart, ptCamera->tBuffers[i].iLength);
	if (ptCamera->iFd >= 0)
		close(ptCamera->iFd);
	ptCamera->iFd = -1;
	ptCamera->iStreaming = 0;
	ptCamera->iMappedCount = 0;
}
