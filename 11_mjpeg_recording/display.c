#include "display.h"

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <jpeglib.h>
#include <setjmp.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

struct jpeg_error
{
	struct jpeg_error_mgr tBase;
	jmp_buf tJump;
};

static void JpegErrorExit(j_common_ptr ptInfo)
{
	struct jpeg_error *ptError = (struct jpeg_error *)ptInfo->err;

	(*ptInfo->err->output_message)(ptInfo);
	longjmp(ptError->tJump, 1);
}

int DisplayInitDevice(struct display_device *ptDisplay, const char *strDevName)
{
	uint32_t uiBlack;
	uint32_t *puiRow;
	int x;
	int y;

	memset(ptDisplay, 0, sizeof(*ptDisplay));
	ptDisplay->iFd = -1;
	ptDisplay->iFd = open(strDevName, O_RDWR);
	if (ptDisplay->iFd < 0)
	{
		perror("open framebuffer");
		return -1;
	}

	if (ioctl(ptDisplay->iFd, FBIOGET_VSCREENINFO, &ptDisplay->tVar) < 0 ||
	    ioctl(ptDisplay->iFd, FBIOGET_FSCREENINFO, &ptDisplay->tFix) < 0)
	{
		perror("get framebuffer info");
		goto err_exit;
	}
	printf("LCD: %ux%u, %u bpp, stride %u\n",
	       ptDisplay->tVar.xres, ptDisplay->tVar.yres,
	       ptDisplay->tVar.bits_per_pixel, ptDisplay->tFix.line_length);
	printf("RGB bit offsets: R=%u G=%u B=%u A=%u\n",
	       ptDisplay->tVar.red.offset, ptDisplay->tVar.green.offset,
	       ptDisplay->tVar.blue.offset, ptDisplay->tVar.transp.offset);

	/* 首版只处理本开发板实测的 32 位、每个颜色通道 8 位布局。 */
	if (ptDisplay->tVar.bits_per_pixel != 32 ||
	    ptDisplay->tVar.red.length != 8 ||
	    ptDisplay->tVar.green.length != 8 ||
	    ptDisplay->tVar.blue.length != 8 ||
	    ptDisplay->tVar.red.offset > 24 ||
	    ptDisplay->tVar.green.offset > 24 ||
	    ptDisplay->tVar.blue.offset > 24 ||
	    (ptDisplay->tVar.transp.length != 0 &&
	     ptDisplay->tVar.transp.length != 8) ||
	    (ptDisplay->tVar.transp.length == 8 &&
	     ptDisplay->tVar.transp.offset > 24))
	{
		fprintf(stderr, "Unsupported framebuffer pixel layout.\n");
		goto err_exit;
	}
	if (ptDisplay->tFix.line_length % 4 != 0 ||
	    ptDisplay->tFix.line_length <
	    (ptDisplay->tVar.xoffset + ptDisplay->tVar.xres) * 4 ||
	    ptDisplay->tFix.smem_len <
	    (size_t)(ptDisplay->tVar.yoffset + ptDisplay->tVar.yres) *
	    ptDisplay->tFix.line_length)
	{
		fprintf(stderr, "Framebuffer memory is smaller than the visible area.\n");
		goto err_exit;
	}

	ptDisplay->iFbMemLength = ptDisplay->tFix.smem_len;
	ptDisplay->pucFbMem = mmap(NULL, ptDisplay->iFbMemLength,
					  PROT_READ | PROT_WRITE, MAP_SHARED,
					  ptDisplay->iFd, 0);
	if (ptDisplay->pucFbMem == MAP_FAILED)
	{
		ptDisplay->pucFbMem = NULL;
		perror("mmap framebuffer");
		goto err_exit;
	}

	/* 清出黑色背景；相机 640x480 图像随后居中显示。 */
	uiBlack = 0;
	if (ptDisplay->tVar.transp.length == 8)
		uiBlack = (uint32_t)255 << ptDisplay->tVar.transp.offset;
	for (y = 0; y < (int)ptDisplay->tVar.yres; y++)
	{
		puiRow = (uint32_t *)(ptDisplay->pucFbMem +
			 (y + ptDisplay->tVar.yoffset) * ptDisplay->tFix.line_length);
		for (x = 0; x < (int)ptDisplay->tVar.xres; x++)
			puiRow[ptDisplay->tVar.xoffset + x] = uiBlack;
	}
	return 0;

err_exit:
	DisplayExitDevice(ptDisplay);
	return -1;
}

int DisplayMjpeg(struct display_device *ptDisplay,
		 const unsigned char *pucJpeg, unsigned int iJpegLength)
{
	struct jpeg_decompress_struct tJpegInfo;
	struct jpeg_error tJpegError;
	JSAMPARRAY ppucRow;
	uint32_t *puiFbRow;
	uint32_t uiPixel;
	unsigned int iLeft;
	unsigned int iTop;
	unsigned int x;
	unsigned int y;
	volatile int iCreated;

	memset(&tJpegInfo, 0, sizeof(tJpegInfo));
	tJpegInfo.err = jpeg_std_error(&tJpegError.tBase);
	tJpegError.tBase.error_exit = JpegErrorExit;
	iCreated = 0;
	if (setjmp(tJpegError.tJump))
	{
		if (iCreated)
			jpeg_destroy_decompress(&tJpegInfo);
		return -1;
	}
	jpeg_create_decompress(&tJpegInfo);
	iCreated = 1;
	jpeg_mem_src(&tJpegInfo, (unsigned char *)pucJpeg, iJpegLength);
	if (jpeg_read_header(&tJpegInfo, TRUE) != JPEG_HEADER_OK)
		goto err_exit;
	tJpegInfo.out_color_space = JCS_RGB;
	jpeg_start_decompress(&tJpegInfo);
	if (tJpegInfo.output_components != 3 ||
	    tJpegInfo.output_width > ptDisplay->tVar.xres ||
	    tJpegInfo.output_height > ptDisplay->tVar.yres)
	{
		fprintf(stderr, "JPEG dimensions or color format are unsupported.\n");
		goto err_exit;
	}

	iLeft = (ptDisplay->tVar.xres - tJpegInfo.output_width) / 2;
	iTop = (ptDisplay->tVar.yres - tJpegInfo.output_height) / 2;
	ppucRow = (*tJpegInfo.mem->alloc_sarray)((j_common_ptr)&tJpegInfo,
						 JPOOL_IMAGE,
						 tJpegInfo.output_width * 3, 1);
	while (tJpegInfo.output_scanline < tJpegInfo.output_height)
	{
		if (jpeg_read_scanlines(&tJpegInfo, ppucRow, 1) != 1)
			goto err_exit;
		y = tJpegInfo.output_scanline - 1;
		puiFbRow = (uint32_t *)(ptDisplay->pucFbMem +
			(ptDisplay->tVar.yoffset + iTop + y) *
			ptDisplay->tFix.line_length);
		puiFbRow += ptDisplay->tVar.xoffset + iLeft;
		for (x = 0; x < tJpegInfo.output_width; x++)
		{
			uiPixel = (uint32_t)ppucRow[0][x * 3] <<
				  ptDisplay->tVar.red.offset;
			uiPixel |= (uint32_t)ppucRow[0][x * 3 + 1] <<
				   ptDisplay->tVar.green.offset;
			uiPixel |= (uint32_t)ppucRow[0][x * 3 + 2] <<
				   ptDisplay->tVar.blue.offset;
			if (ptDisplay->tVar.transp.length == 8)
				uiPixel |= (uint32_t)255 << ptDisplay->tVar.transp.offset;
			puiFbRow[x] = uiPixel;
		}
	}

	jpeg_finish_decompress(&tJpegInfo);
	jpeg_destroy_decompress(&tJpegInfo);
	return 0;

err_exit:
	jpeg_destroy_decompress(&tJpegInfo);
	return -1;
}

void DisplayExitDevice(struct display_device *ptDisplay)
{
	if (ptDisplay->pucFbMem != NULL)
		munmap(ptDisplay->pucFbMem, ptDisplay->iFbMemLength);
	if (ptDisplay->iFd >= 0)
		close(ptDisplay->iFd);
	ptDisplay->pucFbMem = NULL;
	ptDisplay->iFd = -1;
}
