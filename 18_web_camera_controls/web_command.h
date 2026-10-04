#ifndef WEB_COMMAND_H
#define WEB_COMMAND_H

#include "camera_control.h"

#include <pthread.h>

struct camera_device;
struct camera_frame;

enum web_command_result
{
	WEB_COMMAND_ACCEPTED = 0,
	WEB_COMMAND_OFFLINE = -1,
	WEB_COMMAND_BUSY = -2,
	WEB_COMMAND_INVALID = -3
};

struct web_status
{
	int iCameraOnline;
	int iRecording;
	int iSnapshotPending;
	int iRecordPending;
	unsigned int iRemoteSnapshots;
};

struct web_control_status
{
	struct camera_control_values tValues;
	int iValuesValid;
	int iControlPending;
	char acMessage[128];
};

struct web_command
{
	pthread_mutex_t tLock;
	struct camera_control_values tControlValues;
	unsigned int iControlId;
	int iControlValue;
	int iCameraOnline;
	int iRecording;
	int iSnapshotPending;
	int iRecordPending;
	int iControlPending;
	int iControlValuesValid;
	unsigned int iRemoteSnapshots;
	char acControlMessage[128];
};

int WebCommandInit(struct web_command *ptCommand);
void WebCommandExit(struct web_command *ptCommand);
void WebCommandSetCameraOnline(struct web_command *ptCommand, int iOnline);
void WebCommandRefreshControls(struct web_command *ptCommand,
			       const struct camera_device *ptCamera);
enum web_command_result WebCommandRequestSnapshot(struct web_command *ptCommand);
enum web_command_result WebCommandRequestRecordToggle(struct web_command *ptCommand);
enum web_command_result WebCommandRequestControl(struct web_command *ptCommand,
						 unsigned int iId,
						 int iValue);
void WebCommandProcessFrame(struct web_command *ptCommand,
			    struct camera_device *ptCamera,
			    const struct camera_frame *ptFrame);
void WebCommandGetStatus(struct web_command *ptCommand,
			 struct web_status *ptStatus);
void WebCommandGetControlStatus(struct web_command *ptCommand,
				struct web_control_status *ptStatus);

#endif
