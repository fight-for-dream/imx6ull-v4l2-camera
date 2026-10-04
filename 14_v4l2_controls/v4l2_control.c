/*
 * 第一阶段只读取控制项，不修改摄像头参数。
 * 用法：./v4l2_control <camera-by-id-path> list
 */
#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <stdio.h>
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
		printf("  current: not read by this first-stage tool\n");
	}

	if (ptQuery->type == V4L2_CTRL_TYPE_MENU ||
	    ptQuery->type == V4L2_CTRL_TYPE_INTEGER_MENU)
	{
		printf("  menu items:\n");
		PrintMenuItems(iFd, ptQuery);
	}
	printf("\n");
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
		if (ioctl(iFd, VIDIOC_QUERYCTRL, &tQuery) < 0)
			continue;
		if (tQuery.flags & V4L2_CTRL_FLAG_DISABLED)
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

	/* 兼容不支持NEXT_CTRL标志的旧驱动。 */
	iCount += ListControlRange(iFd, V4L2_CID_BASE,
				   V4L2_CID_LASTP1);
	iCount += ListControlRange(iFd, V4L2_CID_CAMERA_CLASS_BASE,
				   V4L2_CID_CAMERA_CLASS_BASE + 64);
	iCount += ListControlRange(iFd, V4L2_CID_PRIVATE_BASE,
				   V4L2_CID_PRIVATE_BASE + 32);
	return iCount;
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

int main(int argc, char *argv[])
{
	int iFd;
	int iCount;

	if (argc != 3 || strcmp(argv[2], "list") != 0)
	{
		printf("Usage: %s <camera-by-id-path> list\n", argv[0]);
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

	printf("Supported controls:\n\n");
	iCount = ListControls(iFd);
	printf("Total supported controls: %d\n", iCount);
	close(iFd);
	return iCount > 0 ? 0 : 1;
}
