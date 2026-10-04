#include "web_command.h"

#include "camera.h"
#include "record.h"
#include "snapshot.h"

#include <stdio.h>
#include <string.h>

int WebCommandInit(struct web_command *ptCommand)
{
	memset(ptCommand, 0, sizeof(*ptCommand));
	snprintf(ptCommand->acControlMessage,
		 sizeof(ptCommand->acControlMessage), "等待摄像头连接");
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
		ptCommand->iControlPending = 0;
		ptCommand->iControlValuesValid = 0;
		snprintf(ptCommand->acControlMessage,
			 sizeof(ptCommand->acControlMessage), "摄像头离线");
	}
	pthread_mutex_unlock(&ptCommand->tLock);
}

void WebCommandRefreshControls(struct web_command *ptCommand,
			       const struct camera_device *ptCamera)
{
	struct camera_control_values tValues;
	int iValid;

	iValid = CameraControlReadValues(ptCamera->iFd, &tValues) == 0;
	pthread_mutex_lock(&ptCommand->tLock);
	ptCommand->iControlValuesValid = iValid;
	if (iValid)
	{
		ptCommand->tControlValues = tValues;
		snprintf(ptCommand->acControlMessage,
			 sizeof(ptCommand->acControlMessage), "控制参数已读取");
	}
	else
	{
		snprintf(ptCommand->acControlMessage,
			 sizeof(ptCommand->acControlMessage), "控制参数读取失败");
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

enum web_command_result WebCommandRequestControl(struct web_command *ptCommand,
						 unsigned int iId,
						 int iValue)
{
	enum web_command_result eResult;

	pthread_mutex_lock(&ptCommand->tLock);
	if (!CameraControlIsWebControl(iId))
		eResult = WEB_COMMAND_INVALID;
	else if (!ptCommand->iCameraOnline)
		eResult = WEB_COMMAND_OFFLINE;
	else if (ptCommand->iControlPending)
		eResult = WEB_COMMAND_BUSY;
	else
	{
		ptCommand->iControlId = iId;
		ptCommand->iControlValue = iValue;
		ptCommand->iControlPending = 1;
		snprintf(ptCommand->acControlMessage,
			 sizeof(ptCommand->acControlMessage), "控制命令等待执行");
		eResult = WEB_COMMAND_ACCEPTED;
	}
	pthread_mutex_unlock(&ptCommand->tLock);
	return eResult;
}

struct pending_web_commands
{
	unsigned int iControlId;
	int iControlValue;
	int iSaveSnapshot;
	int iToggleRecord;
	int iSetControl;
};

struct control_execution
{
	struct camera_control_values tValues;
	enum camera_control_result eResult;
	int iActualValue;
	int iValuesValid;
};

static void TakePendingCommands(struct web_command *ptCommand,
				struct pending_web_commands *ptPending)
{
	pthread_mutex_lock(&ptCommand->tLock);
	ptPending->iSaveSnapshot = ptCommand->iSnapshotPending;
	ptPending->iToggleRecord = ptCommand->iRecordPending;
	ptPending->iSetControl = ptCommand->iControlPending;
	ptPending->iControlId = ptCommand->iControlId;
	ptPending->iControlValue = ptCommand->iControlValue;
	ptCommand->iSnapshotPending = 0;
	ptCommand->iRecordPending = 0;
	ptCommand->iControlPending = 0;
	pthread_mutex_unlock(&ptCommand->tLock);
}

static void ExecuteControl(struct camera_device *ptCamera,
			   const struct pending_web_commands *ptPending,
			   struct control_execution *ptExecution)
{
	memset(ptExecution, 0, sizeof(*ptExecution));
	if (!ptPending->iSetControl)
		return;

	ptExecution->eResult = CameraControlSetValue(ptCamera->iFd,
						      ptPending->iControlId,
						      ptPending->iControlValue,
						      &ptExecution->iActualValue);
	if (ptExecution->eResult == CAMERA_CONTROL_OK)
		ptExecution->iValuesValid =
			CameraControlReadValues(ptCamera->iFd,
						&ptExecution->tValues) == 0;
}

static void UpdateCommandState(struct web_command *ptCommand,
			       const struct pending_web_commands *ptPending,
			       const struct control_execution *ptExecution,
			       int iSnapshotSaved)
{
	int iRecording;

	iRecording = RecordIsActive();
	pthread_mutex_lock(&ptCommand->tLock);
	ptCommand->iRecording = iRecording;
	if (iSnapshotSaved)
		ptCommand->iRemoteSnapshots++;
	if (ptPending->iSetControl)
	{
		if (ptExecution->eResult == CAMERA_CONTROL_OK)
		{
			ptCommand->iControlValuesValid = ptExecution->iValuesValid;
			if (ptExecution->iValuesValid)
				ptCommand->tControlValues = ptExecution->tValues;
			snprintf(ptCommand->acControlMessage,
				 sizeof(ptCommand->acControlMessage),
				 "%s: requested %d, actual %d",
				 CameraControlName(ptPending->iControlId),
				 ptPending->iControlValue,
				 ptExecution->iActualValue);
		}
		else
		{
			snprintf(ptCommand->acControlMessage,
				 sizeof(ptCommand->acControlMessage),
				 "%s: %s",
				 CameraControlName(ptPending->iControlId),
				 CameraControlResultText(ptExecution->eResult));
		}
	}
	pthread_mutex_unlock(&ptCommand->tLock);
}

void WebCommandProcessFrame(struct web_command *ptCommand,
			    struct camera_device *ptCamera,
			    const struct camera_frame *ptFrame)
{
	struct pending_web_commands tPending;
	struct control_execution tControlExecution;
	int iSnapshotSaved = 0;

	/* HTTP线程只提交命令；本函数在摄像头主线程中执行。 */
	TakePendingCommands(ptCommand, &tPending);
	if (tPending.iSaveSnapshot &&
	    SaveJpegSnapshot(ptFrame->pData, ptFrame->iLength) == 0)
		iSnapshotSaved = 1;
	if (tPending.iToggleRecord)
		RecordToggle();
	ExecuteControl(ptCamera, &tPending, &tControlExecution);
	UpdateCommandState(ptCommand, &tPending,
			   &tControlExecution, iSnapshotSaved);
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

void WebCommandGetControlStatus(struct web_command *ptCommand,
				struct web_control_status *ptStatus)
{
	pthread_mutex_lock(&ptCommand->tLock);
	ptStatus->tValues = ptCommand->tControlValues;
	ptStatus->iValuesValid = ptCommand->iControlValuesValid;
	ptStatus->iControlPending = ptCommand->iControlPending;
	snprintf(ptStatus->acMessage, sizeof(ptStatus->acMessage), "%s",
		 ptCommand->acControlMessage);
	pthread_mutex_unlock(&ptCommand->tLock);
}
