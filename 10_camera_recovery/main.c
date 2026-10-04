/*
 * Ubuntu 编译：make
 * 开发板运行：./camera_recovery <camera-by-id-path> /dev/fb0
 * 终端输入 s 回车抓拍，输入 q 回车退出；Ctrl+C 也可退出。
 */
#include "camera.h"
#include "display.h"
#include "http_server.h"
#include "snapshot.h"

#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t g_iStop;
static int g_iInputClosed;

static void StopHandler(int iSignal)
{
	(void)iSignal;
	g_iStop = 1;
}

static int GetKeyboardCommand(int iTimeoutMs)
{
	struct pollfd tPollFd;
	char acInput[32];
	ssize_t iLength;
	int iRet;

	/* 没有交互终端时，仍按指定时间等待，避免重试空转。 */
	if (g_iInputClosed)
	{
		poll(NULL, 0, iTimeoutMs);
		return 0;
	}
	tPollFd.fd = STDIN_FILENO;
	tPollFd.events = POLLIN;
	tPollFd.revents = 0;
	iRet = poll(&tPollFd, 1, iTimeoutMs);
	if (iRet <= 0)
		return 0;
	if (!(tPollFd.revents & POLLIN))
	{
		if (tPollFd.revents & (POLLHUP | POLLERR | POLLNVAL))
			g_iInputClosed = 1;
		return 0;
	}
	iLength = read(STDIN_FILENO, acInput, sizeof(acInput));
	if (iLength <= 0)
	{
		if (iLength == 0)
			g_iInputClosed = 1;
		return 0;
	}
	return acInput[0];
}

int main(int argc, char *argv[])
{
	struct camera_device tCamera;
	struct display_device tDisplay;
	struct http_server tServer;
	struct camera_frame tFrame;
	struct timespec tStart;
	struct timespec tNow;
	enum camera_frame_result eFrameResult;
	unsigned int uiLastSequence;
	unsigned int uiSequenceGaps;
	int iHasSequence;
	int iFrameCount;
	int iRecentFrames;
	int iSnapshotCount;
	int iDisplayRet;
	int iCommand;
	int iCameraReady;
	int iEverConnected;
	int iRetryAttempts;
	int iConsecutiveRetry;
	int iConsecutiveBadJpeg;
	int iResult;
	double dSeconds;

	if (argc != 3)
	{
		printf("Usage: %s <camera-by-id-path> </dev/fbN>\n", argv[0]);
		return 1;
	}

	if (DisplayInitDevice(&tDisplay, argv[2]) < 0)
		return 1;
	if (HttpServerInit(&tServer, 8080) < 0)
	{
		DisplayExitDevice(&tDisplay);
		return 1;
	}
	signal(SIGINT, StopHandler);
	signal(SIGTERM, StopHandler);
	iResult = 1;
	iFrameCount = 0;
	iRecentFrames = 0;
	iSnapshotCount = 0;
	iHasSequence = 0;
	uiLastSequence = 0;
	uiSequenceGaps = 0;
	iCameraReady = 0;
	iEverConnected = 0;
	iRetryAttempts = 0;
	iConsecutiveRetry = 0;
	iConsecutiveBadJpeg = 0;
	printf("Type s + Enter to save a JPEG, q + Enter to quit.\n");
	printf("Using camera path: %s\n", argv[1]);

	while (!g_iStop)
	{
		/* 摄像头不存在时只重试它；LCD 和 HTTP 服务继续运行。 */
		if (!iCameraReady)
		{
			if (CameraInitDevice(&tCamera, argv[1]) < 0)
			{
				iRetryAttempts++;
				if (iRetryAttempts == 1 || iRetryAttempts % 10 == 0)
					printf("Camera unavailable, retrying every 2 seconds.\n");
				iCommand = GetKeyboardCommand(2000);
				if (iCommand == 'q' || iCommand == 'Q')
					g_iStop = 1;
				else if (iCommand == 's' || iCommand == 'S')
					printf("Snapshot unavailable while camera is offline.\n");
				continue;
			}
			iCameraReady = 1;
			iRetryAttempts = 0;
			iConsecutiveRetry = 0;
			iConsecutiveBadJpeg = 0;
			iHasSequence = 0;
			iRecentFrames = 0;
			if (clock_gettime(CLOCK_MONOTONIC, &tStart) < 0)
			{
				perror("clock_gettime");
				goto exit;
			}
			if (iEverConnected)
				printf("Camera reconnected; preview resumed.\n");
			else
				printf("Camera ready.\n");
			iEverConnected = 1;
		}

		/* 取帧后、还帧前，MJPG 数据属于本程序，可以用于显示或抓拍。 */
		eFrameResult = CameraGetFrame(&tCamera, &tFrame);
		if (eFrameResult == CAMERA_FRAME_RETRY)
		{
			iConsecutiveRetry++;
			iCommand = GetKeyboardCommand(0);
			if (iCommand == 'q' || iCommand == 'Q')
				g_iStop = 1;
			if (iConsecutiveRetry >= 3)
			{
				fprintf(stderr, "Three frames failed; reopening camera.\n");
				CameraExitDevice(&tCamera);
				iCameraReady = 0;
			}
			continue;
		}
		if (eFrameResult == CAMERA_FRAME_RESTART)
		{
			fprintf(stderr, "Camera unavailable; releasing old buffers.\n");
			CameraExitDevice(&tCamera);
			iCameraReady = 0;
			continue;
		}
		iConsecutiveRetry = 0;
		if (iHasSequence && tFrame.iSequence > uiLastSequence + 1)
			uiSequenceGaps += tFrame.iSequence - uiLastSequence - 1;
		uiLastSequence = tFrame.iSequence;
		iHasSequence = 1;

		iDisplayRet = DisplayMjpeg(&tDisplay, tFrame.pData, tFrame.iLength);
		if (iDisplayRet == 0)
		{
			iCommand = GetKeyboardCommand(0);
			if (iCommand == 's' || iCommand == 'S')
			{
				if (SaveJpegSnapshot(tFrame.pData, tFrame.iLength,
						     iSnapshotCount + 1) == 0)
					iSnapshotCount++;
			}
			else if (iCommand == 'q' || iCommand == 'Q')
				g_iStop = 1;
			/* 网络线程得到一份拷贝后，摄像头缓冲区才可以重新入队。 */
			if (HttpServerPublish(&tServer, tFrame.pData, tFrame.iLength) < 0)
				fprintf(stderr, "Could not publish frame to HTTP server.\n");
		}

		if (CameraPutFrame(&tCamera, &tFrame) < 0)
		{
			fprintf(stderr, "Could not return frame; reopening camera.\n");
			CameraExitDevice(&tCamera);
			iCameraReady = 0;
			continue;
		}
		if (iDisplayRet < 0)
		{
			iConsecutiveBadJpeg++;
			if (iConsecutiveBadJpeg >= 3)
			{
				fprintf(stderr, "Three JPEG decodes failed; reopening camera.\n");
				CameraExitDevice(&tCamera);
				iCameraReady = 0;
			}
			continue;
		}
		iConsecutiveBadJpeg = 0;
		iFrameCount++;
		iRecentFrames++;
		if (iRecentFrames == 100)
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
			iRecentFrames = 0;
		}
	}
	iResult = 0;

exit:
	printf("Preview stopped: %d frames, %d snapshots.\n",
	       iFrameCount, iSnapshotCount);
	HttpServerExit(&tServer);
	if (iCameraReady)
		CameraExitDevice(&tCamera);
	DisplayExitDevice(&tDisplay);
	return iResult;
}
