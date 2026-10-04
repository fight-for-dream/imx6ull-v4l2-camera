#ifndef WEB_COMMAND_H
#define WEB_COMMAND_H

#include <pthread.h>

struct camera_frame;

enum web_command_result
{
	WEB_COMMAND_ACCEPTED = 0,
	WEB_COMMAND_OFFLINE = -1,
	WEB_COMMAND_BUSY = -2
};

struct web_status
{
	int iCameraOnline;
	int iRecording;
	int iSnapshotPending;
	int iRecordPending;
	unsigned int iRemoteSnapshots;
};

struct web_command
{
	pthread_mutex_t tLock;
	int iCameraOnline;
	int iRecording;
	int iSnapshotPending;
	int iRecordPending;
	unsigned int iRemoteSnapshots;
};

int WebCommandInit(struct web_command *ptCommand);
void WebCommandExit(struct web_command *ptCommand);
void WebCommandSetCameraOnline(struct web_command *ptCommand, int iOnline);
enum web_command_result WebCommandRequestSnapshot(struct web_command *ptCommand);
enum web_command_result WebCommandRequestRecordToggle(struct web_command *ptCommand);
void WebCommandProcessFrame(struct web_command *ptCommand,
			    const struct camera_frame *ptFrame);
void WebCommandGetStatus(struct web_command *ptCommand,
			 struct web_status *ptStatus);

#endif
