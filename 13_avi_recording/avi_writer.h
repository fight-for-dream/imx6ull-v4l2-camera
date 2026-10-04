#ifndef AVI_WRITER_H
#define AVI_WRITER_H

struct avi_writer;

struct avi_writer *AviWriterOpen(const char *strFileName,
				 unsigned int iWidth,
				 unsigned int iHeight,
				 unsigned int iFps);
int AviWriterWriteFrame(struct avi_writer *ptWriter,
			const unsigned char *pucJpeg,
			unsigned int iLength);
int AviWriterClose(struct avi_writer *ptWriter);

#endif
