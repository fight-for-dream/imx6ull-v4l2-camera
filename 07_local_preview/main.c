/*
 * Ubuntu 编译：make
 * 开发板运行：./camera_preview /dev/video1 /dev/fb0
 * Ctrl+C 退出。
 */
#include "camera.h"
#include "display.h"

#include <signal.h>
#include <stdio.h>
#include <time.h>

static volatile sig_atomic_t g_iStop;

static void StopHandler(int iSignal)
{
	(void)iSignal;
	g_iStop = 1;
}

int main(int argc, char *argv[])
{
	struct camera_device tCamera;
	struct display_device tDisplay;
	struct camera_frame tFrame;
	struct timespec tStart;
	struct timespec tNow;
	unsigned int uiLastSequence;
	unsigned int uiSequenceGaps;
	int iHasSequence;
	int iFrameCount;
	int iDisplayRet;
	int iResult;
	double dSeconds;

	if (argc != 3)
	{
		printf("Usage: %s </dev/videoN> </dev/fbN>\n", argv[0]);
		return 1;
	}

	/* 三部分：LCD、摄像头、主循环。 */
	if (DisplayInitDevice(&tDisplay, argv[2]) < 0)
		return 1;
	if (CameraInitDevice(&tCamera, argv[1]) < 0)
	{
		DisplayExitDevice(&tDisplay);
		return 1;
	}
	signal(SIGINT, StopHandler);
	signal(SIGTERM, StopHandler);
	iResult = 1;
	iFrameCount = 0;
	iHasSequence = 0;
	uiLastSequence = 0;
	uiSequenceGaps = 0;
	if (clock_gettime(CLOCK_MONOTONIC, &tStart) < 0)
	{
		perror("clock_gettime");
		goto exit;
	}

	while (!g_iStop)
	{
		/* 1. 取出一帧 MJPG，此时摄像头缓冲区属于本程序。 */
		if (CameraGetFrame(&tCamera, &tFrame) < 0)
			goto exit;

		if (iHasSequence && tFrame.iSequence > uiLastSequence + 1)
			uiSequenceGaps += tFrame.iSequence - uiLastSequence - 1;
		uiLastSequence = tFrame.iSequence;
		iHasSequence = 1;

		/* 2. JPEG 解码一行、画一行到 LCD。 */
		iDisplayRet = DisplayMjpeg(&tDisplay, tFrame.pData, tFrame.iLength);

		/* 3. 处理完毕，把这一帧的缓冲区交还摄像头。 */
		if (CameraPutFrame(&tCamera, &tFrame) < 0)
			goto exit;
		if (iDisplayRet < 0)
			goto exit;

		iFrameCount++;
		if (iFrameCount % 100 == 0)
		{
			if (clock_gettime(CLOCK_MONOTONIC, &tNow) < 0)
			{
				perror("clock_gettime");
				goto exit;
			}
			dSeconds = (double)(tNow.tv_sec - tStart.tv_sec) +
				   (double)(tNow.tv_nsec - tStart.tv_nsec) / 1000000000.0;
			if (dSeconds > 0.0)
				printf("Displayed %d frames, recent %.2f fps, sequence gaps %u\n",
				       iFrameCount, 100.0 / dSeconds, uiSequenceGaps);
			tStart = tNow;
		}
	}
	iResult = 0;

exit:
	printf("Preview stopped after %d frames.\n", iFrameCount);
	CameraExitDevice(&tCamera);
	DisplayExitDevice(&tDisplay);
	return iResult;
}
