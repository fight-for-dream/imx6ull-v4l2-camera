#include "avi_writer.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define AVI_HAS_INDEX  0x00000010U
#define AVI_KEY_FRAME  0x00000010U

struct avi_index
{
	uint32_t iOffset;
	uint32_t iLength;
};

struct avi_writer
{
	FILE *pFile;
	struct avi_index *ptIndex;
	unsigned int iIndexCount;
	unsigned int iIndexCapacity;
	unsigned int iWidth;
	unsigned int iHeight;
	unsigned int iFps;
	unsigned int iMaxFrameSize;
	long lRiffSizePos;
	long lMaxBytesPerSecPos;
	long lTotalFramesPos;
	long lMainBufferSizePos;
	long lStreamLengthPos;
	long lStreamBufferSizePos;
	long lBitmapSizePos;
	long lMoviSizePos;
	long lMoviStart;
};

static int WriteBytes(FILE *pFile, const void *pData, size_t iLength)
{
	if (fwrite(pData, 1, iLength, pFile) != iLength)
		return -1;
	return 0;
}

static int WriteFourcc(FILE *pFile, const char acFourcc[4])
{
	return WriteBytes(pFile, acFourcc, 4);
}

static int WriteU16(FILE *pFile, uint16_t iValue)
{
	unsigned char aucData[2];

	aucData[0] = iValue & 0xff;
	aucData[1] = (iValue >> 8) & 0xff;
	return WriteBytes(pFile, aucData, sizeof(aucData));
}

static int WriteU32(FILE *pFile, uint32_t iValue)
{
	unsigned char aucData[4];

	aucData[0] = iValue & 0xff;
	aucData[1] = (iValue >> 8) & 0xff;
	aucData[2] = (iValue >> 16) & 0xff;
	aucData[3] = (iValue >> 24) & 0xff;
	return WriteBytes(pFile, aucData, sizeof(aucData));
}

static int PatchU32(FILE *pFile, long lPosition, uint32_t iValue)
{
	long lCurrent;

	lCurrent = ftell(pFile);
	if (lCurrent < 0 || fseek(pFile, lPosition, SEEK_SET) < 0 ||
	    WriteU32(pFile, iValue) < 0 ||
	    fseek(pFile, lCurrent, SEEK_SET) < 0)
		return -1;
	return 0;
}

static int WriteAviHeader(struct avi_writer *ptWriter)
{
	FILE *pFile = ptWriter->pFile;
	long lHdrlSizePos;
	long lHdrlStart;
	long lStrlSizePos;
	long lStrlStart;
	long lHeaderEnd;

	/* RIFF('AVI ' ... ) */
	if (WriteFourcc(pFile, "RIFF") < 0)
		return -1;
	ptWriter->lRiffSizePos = ftell(pFile);
	if (WriteU32(pFile, 0) < 0 || WriteFourcc(pFile, "AVI ") < 0)
		return -1;

	/* LIST('hdrl' ... ) */
	if (WriteFourcc(pFile, "LIST") < 0)
		return -1;
	lHdrlSizePos = ftell(pFile);
	if (WriteU32(pFile, 0) < 0)
		return -1;
	lHdrlStart = ftell(pFile);
	if (WriteFourcc(pFile, "hdrl") < 0)
		return -1;

	/* avih：整个 AVI 文件的公共信息。 */
	if (WriteFourcc(pFile, "avih") < 0 || WriteU32(pFile, 56) < 0 ||
	    WriteU32(pFile, 1000000U / ptWriter->iFps) < 0)
		return -1;
	ptWriter->lMaxBytesPerSecPos = ftell(pFile);
	if (WriteU32(pFile, 0) < 0 || WriteU32(pFile, 0) < 0 ||
	    WriteU32(pFile, AVI_HAS_INDEX) < 0)
		return -1;
	ptWriter->lTotalFramesPos = ftell(pFile);
	if (WriteU32(pFile, 0) < 0 || WriteU32(pFile, 0) < 0 ||
	    WriteU32(pFile, 1) < 0)
		return -1;
	ptWriter->lMainBufferSizePos = ftell(pFile);
	if (WriteU32(pFile, 0) < 0 ||
	    WriteU32(pFile, ptWriter->iWidth) < 0 ||
	    WriteU32(pFile, ptWriter->iHeight) < 0 ||
	    WriteU32(pFile, 0) < 0 || WriteU32(pFile, 0) < 0 ||
	    WriteU32(pFile, 0) < 0 || WriteU32(pFile, 0) < 0)
		return -1;

	/* LIST('strl' ... )：本文件只有一路 MJPEG 视频流。 */
	if (WriteFourcc(pFile, "LIST") < 0)
		return -1;
	lStrlSizePos = ftell(pFile);
	if (WriteU32(pFile, 0) < 0)
		return -1;
	lStrlStart = ftell(pFile);
	if (WriteFourcc(pFile, "strl") < 0)
		return -1;

	/* strh：视频流的播放速度和帧数。 */
	if (WriteFourcc(pFile, "strh") < 0 || WriteU32(pFile, 56) < 0 ||
	    WriteFourcc(pFile, "vids") < 0 ||
	    WriteFourcc(pFile, "MJPG") < 0 ||
	    WriteU32(pFile, 0) < 0 || WriteU32(pFile, 0) < 0 ||
	    WriteU32(pFile, 0) < 0 || WriteU32(pFile, 1) < 0 ||
	    WriteU32(pFile, ptWriter->iFps) < 0 ||
	    WriteU32(pFile, 0) < 0)
		return -1;
	ptWriter->lStreamLengthPos = ftell(pFile);
	if (WriteU32(pFile, 0) < 0)
		return -1;
	ptWriter->lStreamBufferSizePos = ftell(pFile);
	if (WriteU32(pFile, 0) < 0 || WriteU32(pFile, 0xffffffffU) < 0 ||
	    WriteU32(pFile, 0) < 0 ||
	    WriteU16(pFile, 0) < 0 || WriteU16(pFile, 0) < 0 ||
	    WriteU16(pFile, ptWriter->iWidth) < 0 ||
	    WriteU16(pFile, ptWriter->iHeight) < 0)
		return -1;

	/* strf：解码器需要的图像格式。 */
	if (WriteFourcc(pFile, "strf") < 0 || WriteU32(pFile, 40) < 0 ||
	    WriteU32(pFile, 40) < 0 ||
	    WriteU32(pFile, ptWriter->iWidth) < 0 ||
	    WriteU32(pFile, ptWriter->iHeight) < 0 ||
	    WriteU16(pFile, 1) < 0 || WriteU16(pFile, 24) < 0 ||
	    WriteFourcc(pFile, "MJPG") < 0)
		return -1;
	ptWriter->lBitmapSizePos = ftell(pFile);
	if (WriteU32(pFile, 0) < 0 || WriteU32(pFile, 0) < 0 ||
	    WriteU32(pFile, 0) < 0 || WriteU32(pFile, 0) < 0 ||
	    WriteU32(pFile, 0) < 0)
		return -1;

	lHeaderEnd = ftell(pFile);
	if (lHeaderEnd < 0 ||
	    PatchU32(pFile, lStrlSizePos, lHeaderEnd - lStrlStart) < 0 ||
	    PatchU32(pFile, lHdrlSizePos, lHeaderEnd - lHdrlStart) < 0)
		return -1;

	/* LIST('movi' ... )：后面开始存放每一帧。 */
	if (WriteFourcc(pFile, "LIST") < 0)
		return -1;
	ptWriter->lMoviSizePos = ftell(pFile);
	if (WriteU32(pFile, 0) < 0)
		return -1;
	ptWriter->lMoviStart = ftell(pFile);
	if (WriteFourcc(pFile, "movi") < 0)
		return -1;
	return 0;
}

static int GrowIndex(struct avi_writer *ptWriter)
{
	struct avi_index *ptNew;
	unsigned int iNewCapacity;

	if (ptWriter->iIndexCount < ptWriter->iIndexCapacity)
		return 0;
	iNewCapacity = ptWriter->iIndexCapacity == 0 ?
		256 : ptWriter->iIndexCapacity * 2;
	ptNew = realloc(ptWriter->ptIndex,
			 iNewCapacity * sizeof(struct avi_index));
	if (ptNew == NULL)
		return -1;
	ptWriter->ptIndex = ptNew;
	ptWriter->iIndexCapacity = iNewCapacity;
	return 0;
}

struct avi_writer *AviWriterOpen(const char *strFileName,
				 unsigned int iWidth,
				 unsigned int iHeight,
				 unsigned int iFps)
{
	struct avi_writer *ptWriter;

	if (iWidth == 0 || iWidth > 32767 ||
	    iHeight == 0 || iHeight > 32767 || iFps == 0)
	{
		errno = EINVAL;
		return NULL;
	}
	ptWriter = calloc(1, sizeof(*ptWriter));
	if (ptWriter == NULL)
		return NULL;
	ptWriter->iWidth = iWidth;
	ptWriter->iHeight = iHeight;
	ptWriter->iFps = iFps;
	ptWriter->pFile = fopen(strFileName, "wb");
	if (ptWriter->pFile == NULL)
	{
		free(ptWriter);
		return NULL;
	}
	if (WriteAviHeader(ptWriter) < 0)
	{
		perror("write AVI header");
		fclose(ptWriter->pFile);
		free(ptWriter);
		return NULL;
	}
	return ptWriter;
}

int AviWriterWriteFrame(struct avi_writer *ptWriter,
			const unsigned char *pucJpeg,
			unsigned int iLength)
{
	struct avi_index *ptEntry;
	long lChunkPosition;
	long lOffset;
	unsigned char ucPadding = 0;

	if (ptWriter == NULL || pucJpeg == NULL || iLength == 0)
	{
		errno = EINVAL;
		return -1;
	}
	if (GrowIndex(ptWriter) < 0)
		return -1;
	lChunkPosition = ftell(ptWriter->pFile);
	if (lChunkPosition < 0)
		return -1;
	lOffset = lChunkPosition - ptWriter->lMoviStart;
	if (lOffset < 0 || (unsigned long)lOffset > UINT32_MAX)
	{
		errno = EFBIG;
		return -1;
	}

	if (WriteFourcc(ptWriter->pFile, "00dc") < 0 ||
	    WriteU32(ptWriter->pFile, iLength) < 0 ||
	    WriteBytes(ptWriter->pFile, pucJpeg, iLength) < 0 ||
	    ((iLength & 1) &&
	     WriteBytes(ptWriter->pFile, &ucPadding, 1) < 0))
		return -1;

	ptEntry = &ptWriter->ptIndex[ptWriter->iIndexCount++];
	ptEntry->iOffset = lOffset;
	ptEntry->iLength = iLength;
	if (iLength > ptWriter->iMaxFrameSize)
		ptWriter->iMaxFrameSize = iLength;
	return 0;
}

static int WriteIndex(struct avi_writer *ptWriter)
{
	unsigned int i;

	if (WriteFourcc(ptWriter->pFile, "idx1") < 0 ||
	    WriteU32(ptWriter->pFile, ptWriter->iIndexCount * 16U) < 0)
		return -1;
	for (i = 0; i < ptWriter->iIndexCount; i++)
	{
		if (WriteFourcc(ptWriter->pFile, "00dc") < 0 ||
		    WriteU32(ptWriter->pFile, AVI_KEY_FRAME) < 0 ||
		    WriteU32(ptWriter->pFile, ptWriter->ptIndex[i].iOffset) < 0 ||
		    WriteU32(ptWriter->pFile, ptWriter->ptIndex[i].iLength) < 0)
			return -1;
	}
	return 0;
}

int AviWriterClose(struct avi_writer *ptWriter)
{
	long lMoviEnd;
	long lFileEnd;
	uint32_t iMaxBytesPerSec;
	int iResult = 0;

	if (ptWriter == NULL)
		return 0;
	lMoviEnd = ftell(ptWriter->pFile);
	if (lMoviEnd < 0 || ferror(ptWriter->pFile) ||
	    WriteIndex(ptWriter) < 0)
	{
		iResult = -1;
	}
	else
	{
		lFileEnd = ftell(ptWriter->pFile);
		iMaxBytesPerSec = ptWriter->iMaxFrameSize * ptWriter->iFps;
		if (lFileEnd < 0 ||
		    PatchU32(ptWriter->pFile, ptWriter->lRiffSizePos,
			     lFileEnd - 8) < 0 ||
		    PatchU32(ptWriter->pFile, ptWriter->lMoviSizePos,
			     lMoviEnd - ptWriter->lMoviStart) < 0 ||
		    PatchU32(ptWriter->pFile, ptWriter->lMaxBytesPerSecPos,
			     iMaxBytesPerSec) < 0 ||
		    PatchU32(ptWriter->pFile, ptWriter->lTotalFramesPos,
			     ptWriter->iIndexCount) < 0 ||
		    PatchU32(ptWriter->pFile, ptWriter->lMainBufferSizePos,
			     ptWriter->iMaxFrameSize) < 0 ||
		    PatchU32(ptWriter->pFile, ptWriter->lStreamLengthPos,
			     ptWriter->iIndexCount) < 0 ||
		    PatchU32(ptWriter->pFile, ptWriter->lStreamBufferSizePos,
			     ptWriter->iMaxFrameSize) < 0 ||
		    PatchU32(ptWriter->pFile, ptWriter->lBitmapSizePos,
			     ptWriter->iMaxFrameSize) < 0)
			iResult = -1;
	}
	if (fclose(ptWriter->pFile) < 0)
		iResult = -1;
	free(ptWriter->ptIndex);
	free(ptWriter);
	return iResult;
}
