#include "web_command.h"

#include "camera.h"
#include "record.h"
#include "snapshot.h"

#include <string.h>

int WebCommandInit(struct web_command *ptCommand)
{
	memset(ptCommand, 0, sizeof(*ptCommand));
	return pthread_mutex_init(&ptCommand->tLock, NULL);
}

void WebCommandExit(struct web_command *ptCommand)
{
	pthread_mutex_destroy(&ptCommand->tLock);
}

void WebCommandSetCameraOnline(struct web_command *ptCommand, int iOnline)
{
	pthread_mutex_lock(&ptCommand->tLock);
	ptCommand->iCameraOnline = iOnline;
	if (!iOnline)
	{
		ptCommand->iRecording = 0;
		ptCommand->iSnapshotPending = 0;
		ptCommand->iRecordPending = 0;
	}
	pthread_mutex_unlock(&ptCommand->tLock);
}

enum web_command_result WebCommandRequestSnapshot(struct web_command *ptCommand)
{
	enum web_command_result eResult;

	pthread_mutex_lock(&ptCommand->tLock);
	if (!ptCommand->iCameraOnline)
		eResult = WEB_COMMAND_OFFLINE;
	else if (ptCommand->iSnapshotPending)
		eResult = WEB_COMMAND_BUSY;
	else
	{
		ptCommand->iSnapshotPending = 1;
		eResult = WEB_COMMAND_ACCEPTED;
	}
	pthread_mutex_unlock(&ptCommand->tLock);
	return eResult;
}

enum web_command_result WebCommandRequestRecordToggle(struct web_command *ptCommand)
{
	enum web_command_result eResult;

	pthread_mutex_lock(&ptCommand->tLock);
	if (!ptCommand->iCameraOnline)
		eResult = WEB_COMMAND_OFFLINE;
	else if (ptCommand->iRecordPending)
		eResult = WEB_COMMAND_BUSY;
	else
	{
		ptCommand->iRecordPending = 1;
		eResult = WEB_COMMAND_ACCEPTED;
	}
	pthread_mutex_unlock(&ptCommand->tLock);
	return eResult;
}

void WebCommandProcessFrame(struct web_command *ptCommand,
			    const struct camera_frame *ptFrame)
{
	int iSaveSnapshot;
	int iToggleRecord;
	int iSnapshotSaved = 0;

	/* HTTP线程只置位；JPEG和录像操作仍在摄像头主线程中执行。 */
	pthread_mutex_lock(&ptCommand->tLock);
	iSaveSnapshot = ptCommand->iSnapshotPending;
	iToggleRecord = ptCommand->iRecordPending;
	ptCommand->iSnapshotPending = 0;
	ptCommand->iRecordPending = 0;
	pthread_mutex_unlock(&ptCommand->tLock);

	if (iSaveSnapshot &&
	    SaveJpegSnapshot(ptFrame->pData, ptFrame->iLength) == 0)
		iSnapshotSaved = 1;
	if (iToggleRecord)
		RecordToggle();

	pthread_mutex_lock(&ptCommand->tLock);
	ptCommand->iRecording = RecordIsActive();
	if (iSnapshotSaved)
		ptCommand->iRemoteSnapshots++;
	pthread_mutex_unlock(&ptCommand->tLock);
}

void WebCommandGetStatus(struct web_command *ptCommand,
			 struct web_status *ptStatus)
{
	pthread_mutex_lock(&ptCommand->tLock);
	ptStatus->iCameraOnline = ptCommand->iCameraOnline;
	ptStatus->iRecording = ptCommand->iRecording;
	ptStatus->iSnapshotPending = ptCommand->iSnapshotPending;
	ptStatus->iRecordPending = ptCommand->iRecordPending;
	ptStatus->iRemoteSnapshots = ptCommand->iRemoteSnapshots;
	pthread_mutex_unlock(&ptCommand->tLock);
}
