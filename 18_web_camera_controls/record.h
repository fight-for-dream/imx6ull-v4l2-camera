#ifndef RECORD_H
#define RECORD_H

void RecordToggle(void);
void RecordWriteFrame(const unsigned char *pucJpeg, unsigned int iLength);
void RecordStop(void);
int RecordIsActive(void);

#endif
