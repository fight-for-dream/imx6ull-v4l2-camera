/*
 * 用法：
 * ./v4l2_control <camera-path> list
 * ./v4l2_control <camera-path> get <control-id>
 * ./v4l2_control <camera-path> set <control-id> <value>
 */
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/videodev2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static const char *GetControlTypeName(unsigned int iType)
{
	switch (iType)
	{
	case V4L2_CTRL_TYPE_INTEGER:
		return "integer";
	case V4L2_CTRL_TYPE_BOOLEAN:
		return "boolean";
	case V4L2_CTRL_TYPE_MENU:
		return "menu";
	case V4L2_CTRL_TYPE_BUTTON:
		return "button";
	case V4L2_CTRL_TYPE_INTEGER64:
		return "integer64";
	case V4L2_CTRL_TYPE_CTRL_CLASS:
		return "control class";
	case V4L2_CTRL_TYPE_STRING:
		return "string";
	case V4L2_CTRL_TYPE_BITMASK:
		return "bitmask";
	case V4L2_CTRL_TYPE_INTEGER_MENU:
		return "integer menu";
	default:
		return "unknown";
	}
}

static int IsSimpleControl(unsigned int iType)
{
	return iType == V4L2_CTRL_TYPE_INTEGER ||
	       iType == V4L2_CTRL_TYPE_BOOLEAN ||
	       iType == V4L2_CTRL_TYPE_MENU ||
	       iType == V4L2_CTRL_TYPE_BITMASK ||
	       iType == V4L2_CTRL_TYPE_INTEGER_MENU;
}

static void PrintControlFlags(unsigned int iFlags)
{
	printf("  flags: 0x%08x", iFlags);
	if (iFlags == 0)
		printf(" (none)");
	if (iFlags & V4L2_CTRL_FLAG_GRABBED)
		printf(" grabbed");
	if (iFlags & V4L2_CTRL_FLAG_READ_ONLY)
		printf(" read-only");
	if (iFlags & V4L2_CTRL_FLAG_UPDATE)
		printf(" update");
	if (iFlags & V4L2_CTRL_FLAG_INACTIVE)
		printf(" inactive");
	if (iFlags & V4L2_CTRL_FLAG_VOLATILE)
		printf(" volatile");
	if (iFlags & V4L2_CTRL_FLAG_WRITE_ONLY)
		printf(" write-only");
	printf("\n");
}

static void PrintMenuItems(int iFd, const struct v4l2_queryctrl *ptQuery)
{
	struct v4l2_querymenu tMenu;
	int i;

	for (i = ptQuery->minimum; i <= ptQuery->maximum; i++)
	{
		memset(&tMenu, 0, sizeof(tMenu));
		tMenu.id = ptQuery->id;
		tMenu.index = i;
		if (ioctl(iFd, VIDIOC_QUERYMENU, &tMenu) < 0)
			continue;
		if (ptQuery->type == V4L2_CTRL_TYPE_INTEGER_MENU)
			printf("    %d = %lld\n", i, (long long)tMenu.value);
		else
			printf("    %d = %s\n", i, (char *)tMenu.name);
	}
}

static void PrintControl(int iFd, const struct v4l2_queryctrl *ptQuery)
{
	struct v4l2_control tControl;

	if (ptQuery->type == V4L2_CTRL_TYPE_CTRL_CLASS)
	{
		printf("=== %s ===\n\n", (char *)ptQuery->name);
		return;
	}
	printf("[0x%08x] %s\n", ptQuery->id, (char *)ptQuery->name);
	printf("  type: %s\n", GetControlTypeName(ptQuery->type));
	printf("  range: %d .. %d, step %d, default %d\n",
	       ptQuery->minimum, ptQuery->maximum,
	       ptQuery->step, ptQuery->default_value);
	PrintControlFlags(ptQuery->flags);

	if (IsSimpleControl(ptQuery->type) &&
	    !(ptQuery->flags & V4L2_CTRL_FLAG_WRITE_ONLY))
	{
		memset(&tControl, 0, sizeof(tControl));
		tControl.id = ptQuery->id;
		if (ioctl(iFd, VIDIOC_G_CTRL, &tControl) == 0)
			printf("  current: %d\n", tControl.value);
		else
			printf("  current: unavailable (%s)\n", strerror(errno));
	}
	else
	{
		printf("  current: unavailable for this control type\n");
	}

	if (ptQuery->type == V4L2_CTRL_TYPE_MENU ||
	    ptQuery->type == V4L2_CTRL_TYPE_INTEGER_MENU)
	{
		printf("  menu items:\n");
		PrintMenuItems(iFd, ptQuery);
	}
	printf("\n");
}

static int QueryControl(int iFd, unsigned int iId,
			struct v4l2_queryctrl *ptQuery)
{
	memset(ptQuery, 0, sizeof(*ptQuery));
	ptQuery->id = iId;
	if (ioctl(iFd, VIDIOC_QUERYCTRL, ptQuery) < 0)
	{
		fprintf(stderr, "Control 0x%08x is not supported: %s\n",
			iId, strerror(errno));
		return -1;
	}
	if (ptQuery->flags & V4L2_CTRL_FLAG_DISABLED)
	{
		fprintf(stderr, "Control 0x%08x is disabled.\n", iId);
		return -1;
	}
	return 0;
}

static int ListControlsWithNextFlag(int iFd)
{
	struct v4l2_queryctrl tQuery;
	int iCount = 0;

	memset(&tQuery, 0, sizeof(tQuery));
	tQuery.id = V4L2_CTRL_FLAG_NEXT_CTRL;
	while (ioctl(iFd, VIDIOC_QUERYCTRL, &tQuery) == 0)
	{
		if (!(tQuery.flags & V4L2_CTRL_FLAG_DISABLED))
		{
			PrintControl(iFd, &tQuery);
			if (tQuery.type != V4L2_CTRL_TYPE_CTRL_CLASS)
				iCount++;
		}
		tQuery.id |= V4L2_CTRL_FLAG_NEXT_CTRL;
	}
	if (errno != EINVAL)
		perror("VIDIOC_QUERYCTRL");
	return iCount;
}

static int ListControlRange(int iFd, unsigned int iFirst,
			    unsigned int iLast)
{
	struct v4l2_queryctrl tQuery;
	unsigned int iId;
	int iCount = 0;

	for (iId = iFirst; iId < iLast; iId++)
	{
		memset(&tQuery, 0, sizeof(tQuery));
		tQuery.id = iId;
		if (ioctl(iFd, VIDIOC_QUERYCTRL, &tQuery) < 0 ||
		    (tQuery.flags & V4L2_CTRL_FLAG_DISABLED))
			continue;
		PrintControl(iFd, &tQuery);
		if (tQuery.type != V4L2_CTRL_TYPE_CTRL_CLASS)
			iCount++;
	}
	return iCount;
}

static int ListControls(int iFd)
{
	int iCount;

	iCount = ListControlsWithNextFlag(iFd);
	if (iCount > 0)
		return iCount;
	iCount += ListControlRange(iFd, V4L2_CID_BASE,
				   V4L2_CID_LASTP1);
	iCount += ListControlRange(iFd, V4L2_CID_CAMERA_CLASS_BASE,
				   V4L2_CID_CAMERA_CLASS_BASE + 64);
	iCount += ListControlRange(iFd, V4L2_CID_PRIVATE_BASE,
				   V4L2_CID_PRIVATE_BASE + 32);
	return iCount;
}

static int ParseControlId(const char *strText, unsigned int *piId)
{
	char *strEnd;
	unsigned long iValue;

	errno = 0;
	iValue = strtoul(strText, &strEnd, 0);
	if (errno != 0 || strEnd == strText || *strEnd != '\0' ||
	    iValue > UINT_MAX)
	{
		fprintf(stderr, "Invalid control ID: %s\n", strText);
		return -1;
	}
	*piId = iValue;
	return 0;
}

static int ParseControlValue(const char *strText, int *piValue)
{
	char *strEnd;
	long iValue;

	errno = 0;
	iValue = strtol(strText, &strEnd, 0);
	if (errno != 0 || strEnd == strText || *strEnd != '\0' ||
	    iValue < INT_MIN || iValue > INT_MAX)
	{
		fprintf(stderr, "Invalid control value: %s\n", strText);
		return -1;
	}
	*piValue = iValue;
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

static int ValidateNewValue(int iFd,
			    const struct v4l2_queryctrl *ptQuery,
			    int iValue)
{
	if (!IsSimpleControl(ptQuery->type))
	{
		fprintf(stderr, "This control type is not handled by VIDIOC_S_CTRL.\n");
		return -1;
	}
	if (ptQuery->flags & V4L2_CTRL_FLAG_READ_ONLY)
	{
		fprintf(stderr, "This control is read-only.\n");
		return -1;
	}
	if (ptQuery->flags & V4L2_CTRL_FLAG_INACTIVE)
	{
		fprintf(stderr, "This control is currently inactive.\n");
		return -1;
	}
	if (iValue < ptQuery->minimum || iValue > ptQuery->maximum)
	{
		fprintf(stderr, "Value must be in the range %d .. %d.\n",
			ptQuery->minimum, ptQuery->maximum);
		return -1;
	}
	if (ptQuery->step > 0 &&
	    (iValue - ptQuery->minimum) % ptQuery->step != 0)
	{
		fprintf(stderr, "Value must follow step %d from minimum %d.\n",
			ptQuery->step, ptQuery->minimum);
		return -1;
	}
	if ((ptQuery->type == V4L2_CTRL_TYPE_MENU ||
	     ptQuery->type == V4L2_CTRL_TYPE_INTEGER_MENU) &&
	    !MenuItemExists(iFd, ptQuery->id, iValue))
	{
		fprintf(stderr, "Menu item %d does not exist.\n", iValue);
		return -1;
	}
	return 0;
}

static int GetControl(int iFd, const char *strId)
{
	struct v4l2_queryctrl tQuery;
	unsigned int iId;

	if (ParseControlId(strId, &iId) < 0 ||
	    QueryControl(iFd, iId, &tQuery) < 0)
		return -1;
	PrintControl(iFd, &tQuery);
	return 0;
}

static int SetControl(int iFd, const char *strId, const char *strValue)
{
	struct v4l2_queryctrl tQuery;
	struct v4l2_control tControl;
	unsigned int iId;
	int iValue;

	if (ParseControlId(strId, &iId) < 0 ||
	    ParseControlValue(strValue, &iValue) < 0 ||
	    QueryControl(iFd, iId, &tQuery) < 0 ||
	    ValidateNewValue(iFd, &tQuery, iValue) < 0)
		return -1;

	memset(&tControl, 0, sizeof(tControl));
	tControl.id = iId;
	tControl.value = iValue;
	if (ioctl(iFd, VIDIOC_S_CTRL, &tControl) < 0)
	{
		perror("VIDIOC_S_CTRL");
		return -1;
	}
	if (ioctl(iFd, VIDIOC_G_CTRL, &tControl) < 0)
	{
		perror("VIDIOC_G_CTRL after setting");
		return -1;
	}
	printf("%s: requested %d, driver reports %d\n",
	       (char *)tQuery.name, iValue, tControl.value);
	return 0;
}

static int PrintDeviceInfo(int iFd)
{
	struct v4l2_capability tCap;

	memset(&tCap, 0, sizeof(tCap));
	if (ioctl(iFd, VIDIOC_QUERYCAP, &tCap) < 0)
	{
		perror("VIDIOC_QUERYCAP");
		return -1;
	}
	printf("driver: %s\n", (char *)tCap.driver);
	printf("card: %s\n", (char *)tCap.card);
	printf("bus: %s\n\n", (char *)tCap.bus_info);
	return 0;
}

static void PrintUsage(const char *strProgram)
{
	printf("Usage:\n");
	printf("  %s <camera-path> list\n", strProgram);
	printf("  %s <camera-path> get <control-id>\n", strProgram);
	printf("  %s <camera-path> set <control-id> <value>\n", strProgram);
}

int main(int argc, char *argv[])
{
	int iFd;
	int iRet;

	if (argc < 3)
	{
		PrintUsage(argv[0]);
		return 1;
	}
	iFd = open(argv[1], O_RDWR);
	if (iFd < 0)
	{
		perror("open camera");
		return 1;
	}
	if (PrintDeviceInfo(iFd) < 0)
	{
		close(iFd);
		return 1;
	}

	if (argc == 3 && strcmp(argv[2], "list") == 0)
	{
		printf("Supported controls:\n\n");
		iRet = ListControls(iFd);
		printf("Total supported controls: %d\n", iRet);
		iRet = iRet > 0 ? 0 : -1;
	}
	else if (argc == 4 && strcmp(argv[2], "get") == 0)
	{
		iRet = GetControl(iFd, argv[3]);
	}
	else if (argc == 5 && strcmp(argv[2], "set") == 0)
	{
		iRet = SetControl(iFd, argv[3], argv[4]);
	}
	else
	{
		PrintUsage(argv[0]);
		iRet = -1;
	}

	close(iFd);
	return iRet < 0 ? 1 : 0;
}
