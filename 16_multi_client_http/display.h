#ifndef DISPLAY_H
#define DISPLAY_H

#include <linux/fb.h>
#include <stddef.h>

struct display_device
{
	int iFd;
	unsigned char *pucFbMem;
	size_t iFbMemLength;
	struct fb_var_screeninfo tVar;
	struct fb_fix_screeninfo tFix;
};

int DisplayInitDevice(struct display_device *ptDisplay, const char *strDevName);
int DisplayMjpeg(struct display_device *ptDisplay,
		 const unsigned char *pucJpeg, unsigned int iJpegLength);
void DisplayExitDevice(struct display_device *ptDisplay);

#endif
