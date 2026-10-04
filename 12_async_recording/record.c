#include "record.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define RECORD_QUEUE_SIZE 8
#define RECORD_MAX_SIZE   (100UL * 1024UL * 1024UL)

struct record_frame
{
	unsigned char *pucData;
	unsigned int iLength;
};

/* 录像所需的状态全部放在本模块中，不进入摄像头主循环。 */
struct record_context
{
	FILE *pFile;
	pthread_t tThread;
	pthread_mutex_t tLock;
	pthread_cond_t tFrameReady;
	struct record_frame atQueue[RECORD_QUEUE_SIZE];
	unsigned int iReadPos;
	unsigned int iWritePos;
	unsigned int iQueueCount;
	unsigned int iWrittenFrames;
	unsigned int iDroppedFrames;
	unsigned int iRecordNumber;
	unsigned long iAcceptedBytes;
	unsigned long iWrittenBytes;
	int iStopping;
};

static struct record_context g_tRecorder =
{
	.tLock = PTHREAD_MUTEX_INITIALIZER,
	.tFrameReady = PTHREAD_COND_INITIALIZER,
};

static void FreeQueueLocked(void)
{
	struct record_frame *ptFrame;

	while (g_tRecorder.iQueueCount > 0)
	{
		ptFrame = &g_tRecorder.atQueue[g_tRecorder.iReadPos];
		free(ptFrame->pucData);
		ptFrame->pucData = NULL;
		g_tRecorder.iReadPos =
			(g_tRecorder.iReadPos + 1) % RECORD_QUEUE_SIZE;
		g_tRecorder.iQueueCount--;
	}
}

static void *RecordThread(void *pArgument)
{
	struct record_frame tFrame;

	(void)pArgument;
	while (1)
	{
		pthread_mutex_lock(&g_tRecorder.tLock);
		while (g_tRecorder.iQueueCount == 0 && !g_tRecorder.iStopping)
			pthread_cond_wait(&g_tRecorder.tFrameReady,
					  &g_tRecorder.tLock);
		if (g_tRecorder.iQueueCount == 0 && g_tRecorder.iStopping)
		{
			pthread_mutex_unlock(&g_tRecorder.tLock);
			break;
		}

		tFrame = g_tRecorder.atQueue[g_tRecorder.iReadPos];
		g_tRecorder.iReadPos =
			(g_tRecorder.iReadPos + 1) % RECORD_QUEUE_SIZE;
		g_tRecorder.iQueueCount--;
		pthread_mutex_unlock(&g_tRecorder.tLock);

		if (fwrite(tFrame.pucData, 1, tFrame.iLength,
			   g_tRecorder.pFile) != tFrame.iLength)
		{
			perror("write record file");
			free(tFrame.pucData);
			pthread_mutex_lock(&g_tRecorder.tLock);
			g_tRecorder.iStopping = 1;
			pthread_mutex_unlock(&g_tRecorder.tLock);
			break;
		}
		free(tFrame.pucData);

		pthread_mutex_lock(&g_tRecorder.tLock);
		g_tRecorder.iWrittenFrames++;
		g_tRecorder.iWrittenBytes += tFrame.iLength;
		pthread_mutex_unlock(&g_tRecorder.tLock);
	}
	return NULL;
}

static void RecordStart(void)
{
	char acFileName[64];
	FILE *pFile;
	int iRet;

	g_tRecorder.iRecordNumber++;
	snprintf(acFileName, sizeof(acFileName), "record_%d_%u.mjpg",
		 (int)getpid(), g_tRecorder.iRecordNumber);
	pFile = fopen(acFileName, "wb");
	if (pFile == NULL)
	{
		perror("fopen record file");
		return;
	}

	pthread_mutex_lock(&g_tRecorder.tLock);
	g_tRecorder.pFile = pFile;
	g_tRecorder.iReadPos = 0;
	g_tRecorder.iWritePos = 0;
	g_tRecorder.iQueueCount = 0;
	g_tRecorder.iWrittenFrames = 0;
	g_tRecorder.iDroppedFrames = 0;
	g_tRecorder.iAcceptedBytes = 0;
	g_tRecorder.iWrittenBytes = 0;
	g_tRecorder.iStopping = 0;
	iRet = pthread_create(&g_tRecorder.tThread, NULL, RecordThread, NULL);
	if (iRet != 0)
		g_tRecorder.pFile = NULL;
	pthread_mutex_unlock(&g_tRecorder.tLock);

	if (iRet != 0)
	{
		fprintf(stderr, "pthread_create: %s\n", strerror(iRet));
		fclose(pFile);
		return;
	}
	printf("Asynchronous recording started: %s\n", acFileName);
}

void RecordToggle(void)
{
	int iRecording;

	pthread_mutex_lock(&g_tRecorder.tLock);
	iRecording = g_tRecorder.pFile != NULL;
	pthread_mutex_unlock(&g_tRecorder.tLock);

	if (iRecording)
		RecordStop();
	else
		RecordStart();
}

void RecordWriteFrame(const unsigned char *pucJpeg, unsigned int iLength)
{
	struct record_frame *ptFrame;

	pthread_mutex_lock(&g_tRecorder.tLock);
	if (g_tRecorder.pFile == NULL)
	{
		pthread_mutex_unlock(&g_tRecorder.tLock);
		return;
	}
	if (g_tRecorder.iStopping)
	{
		pthread_mutex_unlock(&g_tRecorder.tLock);
		RecordStop();
		return;
	}
	if (iLength > RECORD_MAX_SIZE - g_tRecorder.iAcceptedBytes)
	{
		pthread_mutex_unlock(&g_tRecorder.tLock);
		printf("Recording reached the 100 MiB limit.\n");
		RecordStop();
		return;
	}
	if (g_tRecorder.iQueueCount == RECORD_QUEUE_SIZE)
	{
		g_tRecorder.iDroppedFrames++;
		pthread_mutex_unlock(&g_tRecorder.tLock);
		return;
	}

	ptFrame = &g_tRecorder.atQueue[g_tRecorder.iWritePos];
	ptFrame->pucData = malloc(iLength);
	if (ptFrame->pucData == NULL)
	{
		pthread_mutex_unlock(&g_tRecorder.tLock);
		perror("allocate record frame");
		return;
	}
	memcpy(ptFrame->pucData, pucJpeg, iLength);
	ptFrame->iLength = iLength;
	g_tRecorder.iWritePos =
		(g_tRecorder.iWritePos + 1) % RECORD_QUEUE_SIZE;
	g_tRecorder.iQueueCount++;
	g_tRecorder.iAcceptedBytes += iLength;
	pthread_cond_signal(&g_tRecorder.tFrameReady);
	pthread_mutex_unlock(&g_tRecorder.tLock);
}

void RecordStop(void)
{
	FILE *pFile;
	unsigned int iFrames;
	unsigned int iDropped;
	unsigned long iBytes;

	pthread_mutex_lock(&g_tRecorder.tLock);
	if (g_tRecorder.pFile == NULL)
	{
		pthread_mutex_unlock(&g_tRecorder.tLock);
		return;
	}
	g_tRecorder.iStopping = 1;
	pthread_cond_signal(&g_tRecorder.tFrameReady);
	pthread_mutex_unlock(&g_tRecorder.tLock);

	/* 等待录像线程把队列中已有的帧写完。 */
	pthread_join(g_tRecorder.tThread, NULL);

	pthread_mutex_lock(&g_tRecorder.tLock);
	FreeQueueLocked();
	pFile = g_tRecorder.pFile;
	iFrames = g_tRecorder.iWrittenFrames;
	iDropped = g_tRecorder.iDroppedFrames;
	iBytes = g_tRecorder.iWrittenBytes;
	g_tRecorder.pFile = NULL;
	pthread_mutex_unlock(&g_tRecorder.tLock);

	if (fclose(pFile) < 0)
		perror("close record file");
	printf("Recording stopped: %u frames, %lu bytes, %u dropped.\n",
	       iFrames, iBytes, iDropped);
}
