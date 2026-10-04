#include "record.h"

#include <stdio.h>
#include <unistd.h>

/* 防止实验时忘记停止录像而占满根文件系统。 */
#define RECORD_MAX_SIZE (100UL * 1024UL * 1024UL)

static FILE *g_pRecordFile;
static unsigned long g_iRecordBytes;
static unsigned int g_iRecordFrames;
static unsigned int g_iRecordNumber;

void RecordToggle(void)
{
	char acFileName[64];

	if (g_pRecordFile != NULL)
	{
		RecordStop();
		return;
	}

	g_iRecordNumber++;
	snprintf(acFileName, sizeof(acFileName), "record_%d_%u.mjpg",
		 (int)getpid(), g_iRecordNumber);
	g_pRecordFile = fopen(acFileName, "wb");
	if (g_pRecordFile == NULL)
	{
		perror("fopen record file");
		return;
	}
	g_iRecordBytes = 0;
	g_iRecordFrames = 0;
	printf("Recording started: %s\n", acFileName);
}

void RecordWriteFrame(const unsigned char *pucJpeg, unsigned int iLength)
{
	if (g_pRecordFile == NULL)
		return;
	if (iLength > RECORD_MAX_SIZE - g_iRecordBytes)
	{
		printf("Recording reached the 100 MiB limit.\n");
		RecordStop();
		return;
	}
	if (fwrite(pucJpeg, 1, iLength, g_pRecordFile) != iLength)
	{
		perror("write record file");
		RecordStop();
		return;
	}
	g_iRecordBytes += iLength;
	g_iRecordFrames++;
}

void RecordStop(void)
{
	if (g_pRecordFile == NULL)
		return;

	if (fclose(g_pRecordFile) < 0)
		perror("close record file");
	g_pRecordFile = NULL;
	printf("Recording stopped: %u frames, %lu bytes.\n",
	       g_iRecordFrames, g_iRecordBytes);
}
