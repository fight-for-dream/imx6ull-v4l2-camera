#include "camera_control.h"

#include <errno.h>
#include <linux/videodev2.h>
#include <string.h>
#include <sys/ioctl.h>

static int IsSimpleControl(unsigned int iType)
{
	return iType == V4L2_CTRL_TYPE_INTEGER ||
	       iType == V4L2_CTRL_TYPE_BOOLEAN ||
	       iType == V4L2_CTRL_TYPE_MENU ||
	       iType == V4L2_CTRL_TYPE_BITMASK ||
	       iType == V4L2_CTRL_TYPE_INTEGER_MENU;
}

static int QueryControl(int iFd, unsigned int iId,
			struct v4l2_queryctrl *ptQuery)
{
	memset(ptQuery, 0, sizeof(*ptQuery));
	ptQuery->id = iId;
	if (ioctl(iFd, VIDIOC_QUERYCTRL, ptQuery) < 0 ||
	    (ptQuery->flags & V4L2_CTRL_FLAG_DISABLED))
		return -1;
	return 0;
}

static int ReadValue(int iFd, unsigned int iId, int *piValue)
{
	struct v4l2_control tControl;

	memset(&tControl, 0, sizeof(tControl));
	tControl.id = iId;
	if (ioctl(iFd, VIDIOC_G_CTRL, &tControl) < 0)
		return -1;
	*piValue = tControl.value;
	return 0;
}

static int MenuItemExists(int iFd, unsigned int iId, int iValue)
{
	struct v4l2_querymenu tMenu;

	memset(&tMenu, 0, sizeof(tMenu));
	tMenu.id = iId;
	tMenu.index = iValue;
	return ioctl(iFd, VIDIOC_QUERYMENU, &tMenu) == 0;
}

int CameraControlIsWebControl(unsigned int iId)
{
	return iId == V4L2_CID_BRIGHTNESS ||
	       iId == V4L2_CID_AUTO_WHITE_BALANCE ||
	       iId == V4L2_CID_WHITE_BALANCE_TEMPERATURE;
}

const char *CameraControlName(unsigned int iId)
{
	if (iId == V4L2_CID_BRIGHTNESS)
		return "Brightness";
	if (iId == V4L2_CID_AUTO_WHITE_BALANCE)
		return "Auto white balance";
	if (iId == V4L2_CID_WHITE_BALANCE_TEMPERATURE)
		return "White balance temperature";
	return "Unknown control";
}

const char *CameraControlResultText(enum camera_control_result eResult)
{
	switch (eResult)
	{
	case CAMERA_CONTROL_OK:
		return "success";
	case CAMERA_CONTROL_UNSUPPORTED:
		return "unsupported control";
	case CAMERA_CONTROL_READ_ONLY:
		return "read-only control";
	case CAMERA_CONTROL_INACTIVE:
		return "control is inactive";
	case CAMERA_CONTROL_OUT_OF_RANGE:
		return "value is outside the valid range";
	case CAMERA_CONTROL_BAD_STEP:
		return "value does not follow the control step";
	case CAMERA_CONTROL_BAD_MENU:
		return "menu item does not exist";
	default:
		return "V4L2 ioctl failed";
	}
}

int CameraControlReadValues(int iFd, struct camera_control_values *ptValues)
{
	struct v4l2_queryctrl tTemperature;

	if (ReadValue(iFd, V4L2_CID_BRIGHTNESS,
		      &ptValues->iBrightness) < 0 ||
	    ReadValue(iFd, V4L2_CID_AUTO_WHITE_BALANCE,
		      &ptValues->iAutoWhiteBalance) < 0 ||
	    QueryControl(iFd, V4L2_CID_WHITE_BALANCE_TEMPERATURE,
			 &tTemperature) < 0 ||
	    ReadValue(iFd, V4L2_CID_WHITE_BALANCE_TEMPERATURE,
		      &ptValues->iWhiteBalanceTemperature) < 0)
		return -1;
	ptValues->iWhiteBalanceTemperatureActive =
		!(tTemperature.flags & V4L2_CTRL_FLAG_INACTIVE);
	return 0;
}

enum camera_control_result CameraControlSetValue(int iFd,
						 unsigned int iId,
						 int iValue,
						 int *piActualValue)
{
	struct v4l2_queryctrl tQuery;
	struct v4l2_control tControl;

	if (!CameraControlIsWebControl(iId) ||
	    QueryControl(iFd, iId, &tQuery) < 0 ||
	    !IsSimpleControl(tQuery.type))
		return CAMERA_CONTROL_UNSUPPORTED;
	if (tQuery.flags & V4L2_CTRL_FLAG_READ_ONLY)
		return CAMERA_CONTROL_READ_ONLY;
	if (tQuery.flags & V4L2_CTRL_FLAG_INACTIVE)
		return CAMERA_CONTROL_INACTIVE;
	if (iValue < tQuery.minimum || iValue > tQuery.maximum)
		return CAMERA_CONTROL_OUT_OF_RANGE;
	if (tQuery.step > 0 &&
	    (iValue - tQuery.minimum) % tQuery.step != 0)
		return CAMERA_CONTROL_BAD_STEP;
	if ((tQuery.type == V4L2_CTRL_TYPE_MENU ||
	     tQuery.type == V4L2_CTRL_TYPE_INTEGER_MENU) &&
	    !MenuItemExists(iFd, iId, iValue))
		return CAMERA_CONTROL_BAD_MENU;

	memset(&tControl, 0, sizeof(tControl));
	tControl.id = iId;
	tControl.value = iValue;
	if (ioctl(iFd, VIDIOC_S_CTRL, &tControl) < 0 ||
	    ioctl(iFd, VIDIOC_G_CTRL, &tControl) < 0)
		return CAMERA_CONTROL_IO_ERROR;
	*piActualValue = tControl.value;
	return CAMERA_CONTROL_OK;
}
