#ifndef CAMERA_CONTROL_H
#define CAMERA_CONTROL_H

enum camera_control_result
{
	CAMERA_CONTROL_OK = 0,
	CAMERA_CONTROL_UNSUPPORTED = -1,
	CAMERA_CONTROL_READ_ONLY = -2,
	CAMERA_CONTROL_INACTIVE = -3,
	CAMERA_CONTROL_OUT_OF_RANGE = -4,
	CAMERA_CONTROL_BAD_STEP = -5,
	CAMERA_CONTROL_BAD_MENU = -6,
	CAMERA_CONTROL_IO_ERROR = -7
};

struct camera_control_values
{
	int iBrightness;
	int iAutoWhiteBalance;
	int iWhiteBalanceTemperature;
	int iWhiteBalanceTemperatureActive;
};

int CameraControlIsWebControl(unsigned int iId);
const char *CameraControlName(unsigned int iId);
const char *CameraControlResultText(enum camera_control_result eResult);
int CameraControlReadValues(int iFd, struct camera_control_values *ptValues);
enum camera_control_result CameraControlSetValue(int iFd,
						 unsigned int iId,
						 int iValue,
						 int *piActualValue);

#endif
