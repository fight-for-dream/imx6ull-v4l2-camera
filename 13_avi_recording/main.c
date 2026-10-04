/*
 * Ubuntu 编译：make
 * 开发板运行：./camera_avi_recorder <camera-by-id-path> /dev/fb0
 * s 抓拍，r 开始或停止录像，q 退出。
 */
#include "camera.h"
#include "display.h"
#include "http_server.h"
#include "record.h"
#include "snapshot.h"

#include <poll.h>
#include <signal.h>
#include <stdio.h>
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
	char acInput[16];
	int iRet;

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
		g_iInputClosed = 1;
		return 0;
	}

	iRet = read(STDIN_FILENO, acInput, sizeof(acInput));
	if (iRet <= 0)
	{
		g_iInputClosed = 1;
		return 0;
	}
	return acInput[0];
}

static void HandleCommand(const struct camera_frame *ptFrame, int iTimeoutMs)
{
	static int iSnapshotCount;
	int iCommand;

	iCommand = GetKeyboardCommand(iTimeoutMs);
	if (iCommand == 'q' || iCommand == 'Q')
	{
		g_iStop = 1;
	}
	else if (iCommand == 'r' || iCommand == 'R')
	{
		if (ptFrame != NULL)
			RecordToggle();
		else
			printf("Recording unavailable while camera is offline.\n");
	}
	else if (iCommand == 's' || iCommand == 'S')
	{
		if (ptFrame == NULL)
		{
			printf("Snapshot unavailable while camera is offline.\n");
		}
		else if (SaveJpegSnapshot(ptFrame->pData, ptFrame->iLength,
					  iSnapshotCount + 1) == 0)
		{
			iSnapshotCount++;
		}
	}
}

static int WaitForCamera(struct camera_device *ptCamera,
			 const char *strCameraPath)
{
	printf("Waiting for camera: %s\n", strCameraPath);
	while (!g_iStop)
	{
		if (CameraInitDevice(ptCamera, strCameraPath) == 0)
			return 0;
		HandleCommand(NULL, 2000);
	}
	return -1;
}

static int CaptureFrames(struct camera_device *ptCamera,
			 struct display_device *ptDisplay,
			 struct http_server *ptServer)
{
	struct camera_frame tFrame;
	enum camera_frame_result eResult;
	int iFailedFrames = 0;
	int iDisplayRet;

	while (!g_iStop)
	{
		eResult = CameraGetFrame(ptCamera, &tFrame);
		if (eResult == CAMERA_FRAME_RESTART)
			return -1;
		if (eResult == CAMERA_FRAME_RETRY)
		{
			HandleCommand(NULL, 0);
			iFailedFrames++;
			if (iFailedFrames >= 3)
				return -1;
			continue;
		}
		iFailedFrames = 0;

		iDisplayRet = DisplayMjpeg(ptDisplay, tFrame.pData, tFrame.iLength);
		HandleCommand(&tFrame, 0);
		if (!g_iStop && iDisplayRet == 0)
		{
			HttpServerPublish(ptServer, tFrame.pData, tFrame.iLength);
			RecordWriteFrame(tFrame.pData, tFrame.iLength);
		}

		if (CameraPutFrame(ptCamera, &tFrame) < 0)
			return -1;
	}
	return 0;
}

int main(int argc, char *argv[])
{
	struct camera_device tCamera;
	struct display_device tDisplay;
	struct http_server tServer;

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
	printf("Commands: s=snapshot, r=start/stop recording, q=quit.\n");

	while (!g_iStop)
	{
		if (WaitForCamera(&tCamera, argv[1]) < 0)
			break;
		printf("Camera ready.\n");
		if (CaptureFrames(&tCamera, &tDisplay, &tServer) < 0)
			printf("Camera unavailable; waiting for reconnection.\n");
		RecordStop();
		CameraExitDevice(&tCamera);
	}

	RecordStop();
	HttpServerExit(&tServer);
	DisplayExitDevice(&tDisplay);
	return 0;
}
