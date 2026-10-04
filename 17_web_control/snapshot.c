#include "snapshot.h"

#include <stdio.h>
#include <unistd.h>

int SaveJpegSnapshot(const unsigned char *pucJpeg,
		     unsigned int iJpegLength)
{
	static int iNumber;
	char strFileName[128];
	FILE *pFile;
	size_t iWritten;
	int iLength;

	/* PID + 序号让本次运行的照片与其他运行区分开。 */
	iNumber++;
	iLength = snprintf(strFileName, sizeof(strFileName),
			   "/tmp/camera_%ld_%03d.jpg", (long)getpid(), iNumber);
	if (iLength < 0 || iLength >= (int)sizeof(strFileName))
	{
		fprintf(stderr, "Snapshot file name is too long.\n");
		return -1;
	}

	pFile = fopen(strFileName, "wb");
	if (pFile == NULL)
	{
		perror("open snapshot file");
		return -1;
	}
	iWritten = fwrite(pucJpeg, 1, iJpegLength, pFile);
	if (iWritten != iJpegLength)
	{
		perror("write snapshot file");
		fclose(pFile);
		unlink(strFileName);
		return -1;
	}
	if (fclose(pFile) != 0)
	{
		perror("close snapshot file");
		unlink(strFileName);
		return -1;
	}

	printf("Saved %u bytes: %s\n", iJpegLength, strFileName);
	return 0;
}
