#ifndef CAMERA_H
#define CAMERA_H

#include <stddef.h>

#define CAMERA_BUFFER_COUNT 4

struct camera_buffer
{
	void *pStart;
	size_t iLength;
};

struct camera_device
{
	int iFd;
	struct camera_buffer tBuffers[CAMERA_BUFFER_COUNT];
	unsigned int iBufferCount;
	int iMappedCount;
	int iStreaming;
};

struct camera_frame
{
	const unsigned char *pData;
	unsigned int iLength;
	unsigned int iIndex;
	unsigned int iSequence;
};

enum camera_frame_result
{
	CAMERA_FRAME_RESTART = -1, /* 旧设备已不可继续采集，需关闭重开 */
	CAMERA_FRAME_OK = 0,
	CAMERA_FRAME_RETRY = 1    /* 暂时无可用帧或一帧损坏，可继续 */
};

int CameraInitDevice(struct camera_device *ptCamera, const char *strDevName);
enum camera_frame_result CameraGetFrame(struct camera_device *ptCamera,
						  struct camera_frame *ptFrame);
int CameraPutFrame(struct camera_device *ptCamera, const struct camera_frame *ptFrame);
void CameraExitDevice(struct camera_device *ptCamera);

#endif
