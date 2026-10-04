#include "http_server.h"
#include "web_command.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

static const char g_acIndexPage[] =
	"<!doctype html>\n"
	"<html lang=\"zh-CN\"><head><meta charset=\"utf-8\">\n"
	"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">\n"
	"<title>i.MX6ULL Camera</title>\n"
	"<style>"
	"body{margin:0;background:#17202a;color:#eef2f5;font-family:sans-serif;text-align:center}"
	"main{max-width:760px;margin:auto;padding:20px}"
	"img{width:100%;max-width:640px;background:#000;border-radius:8px}"
	"button{margin:12px 6px;padding:10px 20px;font-size:16px}"
	".control{max-width:640px;margin:14px auto;text-align:left}"
	".control input[type=range]{width:100%}"
	"#status,#message,#control-message{margin:10px}"
	"</style></head><body><main>\n"
	"<h1>i.MX6ULL Camera</h1>\n"
	"<img src=\"/stream\" alt=\"camera stream\">\n"
	"<div><button onclick=\"sendCommand('/api/snapshot')\">抓拍</button>"
	"<button onclick=\"sendCommand('/api/record/toggle')\">开始/停止录像</button></div>\n"
	"<div id=\"status\">正在读取状态...</div><div id=\"message\"></div>\n"
	"<div class=\"control\"><label>亮度：<span id=\"brightness-value\">0</span></label>"
	"<input id=\"brightness\" type=\"range\" min=\"-255\" max=\"255\" step=\"1\" "
	"onchange=\"setControl('0x00980900',this.value)\"></div>\n"
	"<div class=\"control\"><label><input id=\"auto-wb\" type=\"checkbox\" "
	"onchange=\"setControl('0x0098090c',this.checked?1:0)\"> 自动白平衡</label></div>\n"
	"<div class=\"control\"><label>手动色温：<span id=\"temperature-value\">5000</span> K</label>"
	"<input id=\"temperature\" type=\"range\" min=\"2500\" max=\"7000\" step=\"1\" "
	"onchange=\"setControl('0x0098091a',this.value)\"></div>\n"
	"<div id=\"control-message\">正在读取摄像头参数...</div>\n"
	"<script>\n"
	"async function refreshStatus(){try{"
	"const r=await fetch('/api/status',{cache:'no-store'});const s=await r.json();"
	"document.getElementById('status').textContent="
	"'摄像头：'+(s.camera_online?'在线':'离线')+'；录像：'+"
	"(s.recording?'进行中':'未录像')+'；网页抓拍：'+s.remote_snapshots;"
	"}catch(e){document.getElementById('status').textContent='状态读取失败';}}\n"
	"async function refreshControls(){try{"
	"const r=await fetch('/api/controls',{cache:'no-store'});const c=await r.json();"
	"if(c.valid){"
	"const b=document.getElementById('brightness');b.value=c.brightness;"
	"document.getElementById('brightness-value').textContent=c.brightness;"
	"const a=document.getElementById('auto-wb');a.checked=!!c.auto_white_balance;"
	"const t=document.getElementById('temperature');t.value=c.temperature;"
	"t.disabled=!c.temperature_active;"
	"document.getElementById('temperature-value').textContent=c.temperature;}"
	"document.getElementById('control-message').textContent=c.message;"
	"}catch(e){document.getElementById('control-message').textContent='参数读取失败';}}\n"
	"async function sendCommand(path){try{"
	"const r=await fetch(path,{method:'POST'});const s=await r.json();"
	"document.getElementById('message').textContent=s.message;refreshStatus();"
	"}catch(e){document.getElementById('message').textContent='命令发送失败';}}\n"
	"async function setControl(id,value){try{"
	"const p='/api/control?id='+encodeURIComponent(id)+'&value='+encodeURIComponent(value);"
	"const r=await fetch(p,{method:'POST'});const s=await r.json();"
	"document.getElementById('control-message').textContent=s.message;"
	"setTimeout(refreshControls,200);"
	"}catch(e){document.getElementById('control-message').textContent='参数设置失败';}}\n"
	"async function refreshAll(){await refreshStatus();await refreshControls();}"
	"setInterval(refreshAll,1000);refreshAll();\n"
	"</script></main></body></html>\n";

static int SendAll(int iFd, const void *pData, size_t iLength)
{
	const unsigned char *pucData = pData;
	size_t iSent = 0;
	ssize_t iRet;

	while (iSent < iLength)
	{
		iRet = send(iFd, pucData + iSent, iLength - iSent, MSG_NOSIGNAL);
		if (iRet < 0 && errno == EINTR)
			continue;
		if (iRet <= 0)
			return -1;
		iSent += iRet;
	}
	return 0;
}

static int SendResponse(int iFd, const char *strStatus,
			const char *strContentType, const char *strBody)
{
	char acHeader[256];
	size_t iBodyLength = strlen(strBody);
	int iHeaderLength;

	iHeaderLength = snprintf(acHeader, sizeof(acHeader),
				 "HTTP/1.1 %s\r\n"
				 "Content-Type: %s\r\n"
				 "Content-Length: %zu\r\n"
				 "Cache-Control: no-store\r\n"
				 "Connection: close\r\n\r\n",
				 strStatus, strContentType, iBodyLength);
	if (iHeaderLength < 0 || iHeaderLength >= (int)sizeof(acHeader) ||
	    SendAll(iFd, acHeader, iHeaderLength) < 0 ||
	    SendAll(iFd, strBody, iBodyLength) < 0)
		return -1;
	return 0;
}

static int ActiveClientCountLocked(struct http_server *ptServer)
{
	int iCount = 0;
	int i;

	for (i = 0; i < HTTP_MAX_CLIENTS; i++)
	{
		if (ptServer->atClients[i].iActive)
			iCount++;
	}
	return iCount;
}

static int ServerStopped(struct http_server *ptServer)
{
	int iStop;

	pthread_mutex_lock(&ptServer->tLock);
	iStop = ptServer->iStop;
	pthread_mutex_unlock(&ptServer->tLock);
	return iStop;
}

static void SendStatus(struct http_server *ptServer, int iClientFd)
{
	struct web_status tStatus;
	char acJson[256];

	WebCommandGetStatus(ptServer->ptWebCommand, &tStatus);
	snprintf(acJson, sizeof(acJson),
		 "{\"camera_online\":%s,\"recording\":%s,"
		 "\"snapshot_pending\":%s,\"record_pending\":%s,"
		 "\"remote_snapshots\":%u}\n",
		 tStatus.iCameraOnline ? "true" : "false",
		 tStatus.iRecording ? "true" : "false",
		 tStatus.iSnapshotPending ? "true" : "false",
		 tStatus.iRecordPending ? "true" : "false",
		 tStatus.iRemoteSnapshots);
	SendResponse(iClientFd, "200 OK", "application/json", acJson);
}

static void SendControlStatus(struct http_server *ptServer, int iClientFd)
{
	struct web_control_status tStatus;
	char acJson[512];

	WebCommandGetControlStatus(ptServer->ptWebCommand, &tStatus);
	snprintf(acJson, sizeof(acJson),
		 "{\"valid\":%s,\"pending\":%s,\"brightness\":%d,"
		 "\"auto_white_balance\":%d,\"temperature\":%d,"
		 "\"temperature_active\":%s,\"message\":\"%s\"}\n",
		 tStatus.iValuesValid ? "true" : "false",
		 tStatus.iControlPending ? "true" : "false",
		 tStatus.tValues.iBrightness,
		 tStatus.tValues.iAutoWhiteBalance,
		 tStatus.tValues.iWhiteBalanceTemperature,
		 tStatus.tValues.iWhiteBalanceTemperatureActive ? "true" : "false",
		 tStatus.acMessage);
	SendResponse(iClientFd, "200 OK", "application/json", acJson);
}

static void SendCommandResult(int iClientFd,
			      enum web_command_result eResult,
			      const char *strAcceptedMessage)
{
	if (eResult == WEB_COMMAND_ACCEPTED)
		SendResponse(iClientFd, "202 Accepted", "application/json",
			     strAcceptedMessage);
	else if (eResult == WEB_COMMAND_OFFLINE)
		SendResponse(iClientFd, "409 Conflict", "application/json",
			     "{\"ok\":false,\"message\":\"摄像头离线\"}\n");
	else if (eResult == WEB_COMMAND_INVALID)
		SendResponse(iClientFd, "400 Bad Request", "application/json",
			     "{\"ok\":false,\"message\":\"控制参数无效\"}\n");
	else
		SendResponse(iClientFd, "409 Conflict", "application/json",
			     "{\"ok\":false,\"message\":\"已有同类命令等待执行\"}\n");
}

static int ParseControlRequest(const char *strRequest,
			       unsigned int *piId, int *piValue)
{
	const char *strId;
	const char *strValue;
	char *strEnd;
	unsigned long iId;
	long iValue;

	strId = strstr(strRequest, "id=");
	strValue = strstr(strRequest, "&value=");
	if (strId == NULL || strValue == NULL || strId > strValue)
		return -1;
	errno = 0;
	iId = strtoul(strId + 3, &strEnd, 0);
	if (errno != 0 || strEnd != strValue || iId > UINT_MAX)
		return -1;
	errno = 0;
	iValue = strtol(strValue + 7, &strEnd, 0);
	if (errno != 0 || *strEnd != ' ' ||
	    iValue < INT_MIN || iValue > INT_MAX)
		return -1;
	*piId = iId;
	*piValue = iValue;
	return 0;
}

static void SendMjpegStream(struct http_server *ptServer, int iClientFd)
{
	static const char acHeader[] =
		"HTTP/1.1 200 OK\r\n"
		"Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
		"Cache-Control: no-cache\r\n"
		"Connection: close\r\n\r\n";
	unsigned char *pucFrame = NULL;
	unsigned char *pucNew;
	size_t iCapacity = 0;
	size_t iLength;
	unsigned int iLastGeneration = 0;
	char acPart[128];
	int iPartLength;

	if (SendAll(iClientFd, acHeader, sizeof(acHeader) - 1) < 0)
		return;
	while (1)
	{
		pthread_mutex_lock(&ptServer->tLock);
		while (!ptServer->iStop &&
		       ptServer->iGeneration == iLastGeneration)
			pthread_cond_wait(&ptServer->tFrameReady, &ptServer->tLock);
		if (ptServer->iStop)
		{
			pthread_mutex_unlock(&ptServer->tLock);
			break;
		}
		iLength = ptServer->iFrameLength;
		if (iCapacity < iLength)
		{
			pucNew = realloc(pucFrame, iLength);
			if (pucNew == NULL)
			{
				pthread_mutex_unlock(&ptServer->tLock);
				fprintf(stderr, "HTTP frame buffer allocation failed.\n");
				break;
			}
			pucFrame = pucNew;
			iCapacity = iLength;
		}
		memcpy(pucFrame, ptServer->pucLatestFrame, iLength);
		iLastGeneration = ptServer->iGeneration;
		pthread_mutex_unlock(&ptServer->tLock);

		iPartLength = snprintf(acPart, sizeof(acPart),
				       "--frame\r\n"
				       "Content-Type: image/jpeg\r\n"
				       "Content-Length: %zu\r\n\r\n", iLength);
		if (iPartLength < 0 || iPartLength >= (int)sizeof(acPart) ||
		    SendAll(iClientFd, acPart, iPartLength) < 0 ||
		    SendAll(iClientFd, pucFrame, iLength) < 0 ||
		    SendAll(iClientFd, "\r\n", 2) < 0)
			break;
	}
	free(pucFrame);
}

static int ReadRequestLine(int iClientFd, char *strRequest, size_t iSize)
{
	size_t iLength = 0;
	ssize_t iRead;

	do
	{
		iRead = recv(iClientFd, strRequest + iLength,
			     iSize - 1 - iLength, 0);
		if (iRead <= 0)
			return -1;
		iLength += iRead;
		strRequest[iLength] = '\0';
	} while (strstr(strRequest, "\r\n") == NULL && iLength < iSize - 1);
	return 0;
}

static int HandleClientRequest(struct http_client *ptClient)
{
	struct http_server *ptServer = ptClient->ptServer;
	char acRequest[256];
	unsigned int iControlId;
	int iControlValue;
	int iActiveCount;

	if (ReadRequestLine(ptClient->iFd, acRequest, sizeof(acRequest)) < 0)
		return 0;
	if (strncmp(acRequest, "GET /stream ", 12) == 0)
	{
		pthread_mutex_lock(&ptServer->tLock);
		iActiveCount = ActiveClientCountLocked(ptServer);
		pthread_mutex_unlock(&ptServer->tLock);
		printf("HTTP stream client %d connected (%d active).\n",
		       ptClient->iIndex + 1, iActiveCount);
		SendMjpegStream(ptServer, ptClient->iFd);
		return 1;
	}
	if (strncmp(acRequest, "GET / ", 6) == 0)
	{
		SendResponse(ptClient->iFd, "200 OK", "text/html; charset=utf-8",
			     g_acIndexPage);
		return 0;
	}
	if (strncmp(acRequest, "GET /api/status ", 16) == 0)
	{
		SendStatus(ptServer, ptClient->iFd);
		return 0;
	}
	if (strncmp(acRequest, "GET /api/controls ", 18) == 0)
	{
		SendControlStatus(ptServer, ptClient->iFd);
		return 0;
	}
	if (strncmp(acRequest, "POST /api/snapshot ", 19) == 0)
	{
		SendCommandResult(ptClient->iFd,
			WebCommandRequestSnapshot(ptServer->ptWebCommand),
			"{\"ok\":true,\"message\":\"抓拍命令已提交\"}\n");
		return 0;
	}
	if (strncmp(acRequest, "POST /api/record/toggle ", 24) == 0)
	{
		SendCommandResult(ptClient->iFd,
			WebCommandRequestRecordToggle(ptServer->ptWebCommand),
			"{\"ok\":true,\"message\":\"录像切换命令已提交\"}\n");
		return 0;
	}
	if (strncmp(acRequest, "POST /api/control?", 18) == 0)
	{
		if (ParseControlRequest(acRequest,
					&iControlId, &iControlValue) < 0)
		{
			SendResponse(ptClient->iFd, "400 Bad Request",
				     "application/json",
				     "{\"ok\":false,\"message\":\"请求参数格式错误\"}\n");
		}
		else
		{
			SendCommandResult(ptClient->iFd,
				WebCommandRequestControl(ptServer->ptWebCommand,
							iControlId, iControlValue),
				"{\"ok\":true,\"message\":\"控制命令已提交\"}\n");
		}
		return 0;
	}
	SendResponse(ptClient->iFd, "404 Not Found", "text/plain", "Not found\n");
	return 0;
}

static void *ClientThread(void *pArgument)
{
	struct http_client *ptClient = pArgument;
	struct http_server *ptServer = ptClient->ptServer;
	int iClientFd = ptClient->iFd;
	int iWasStream;
	int iActiveCount;

	iWasStream = HandleClientRequest(ptClient);
	pthread_mutex_lock(&ptServer->tLock);
	ptClient->iFd = -1;
	ptClient->iActive = 0;
	iActiveCount = ActiveClientCountLocked(ptServer);
	pthread_mutex_unlock(&ptServer->tLock);
	close(iClientFd);
	if (iWasStream)
		printf("HTTP stream client %d disconnected (%d active).\n",
		       ptClient->iIndex + 1, iActiveCount);
	return NULL;
}

static void ReapFinishedClients(struct http_server *ptServer)
{
	pthread_t tThread;
	int iJoin;
	int i;

	for (i = 0; i < HTTP_MAX_CLIENTS; i++)
	{
		pthread_mutex_lock(&ptServer->tLock);
		iJoin = ptServer->atClients[i].iThreadStarted &&
			!ptServer->atClients[i].iActive;
		if (iJoin)
		{
			tThread = ptServer->atClients[i].tThread;
			ptServer->atClients[i].iThreadStarted = 0;
		}
		pthread_mutex_unlock(&ptServer->tLock);
		if (iJoin)
			pthread_join(tThread, NULL);
	}
}

static struct http_client *FindFreeClientLocked(struct http_server *ptServer)
{
	int i;

	for (i = 0; i < HTTP_MAX_CLIENTS; i++)
	{
		if (!ptServer->atClients[i].iThreadStarted)
			return &ptServer->atClients[i];
	}
	return NULL;
}

static int SetClientTimeout(int iClientFd)
{
	struct timeval tTimeout;

	tTimeout.tv_sec = 2;
	tTimeout.tv_usec = 0;
	if (setsockopt(iClientFd, SOL_SOCKET, SO_RCVTIMEO,
		       &tTimeout, sizeof(tTimeout)) < 0 ||
	    setsockopt(iClientFd, SOL_SOCKET, SO_SNDTIMEO,
		       &tTimeout, sizeof(tTimeout)) < 0)
		return -1;
	return 0;
}

static void RejectBusyClient(int iClientFd)
{
	SendResponse(iClientFd, "503 Service Unavailable", "text/plain",
		     "Too many clients\n");
	close(iClientFd);
}

static void *AcceptThread(void *pArgument)
{
	struct http_server *ptServer = pArgument;
	struct http_client *ptClient;
	struct pollfd tPollFd;
	int iClientFd;
	int iRet;

	while (!ServerStopped(ptServer))
	{
		ReapFinishedClients(ptServer);
		tPollFd.fd = ptServer->iListenFd;
		tPollFd.events = POLLIN;
		tPollFd.revents = 0;
		iRet = poll(&tPollFd, 1, 500);
		if (iRet < 0 && errno == EINTR)
			continue;
		if (iRet < 0)
		{
			if (!ServerStopped(ptServer))
				perror("poll HTTP socket");
			break;
		}
		if (iRet == 0)
			continue;
		if (tPollFd.revents & (POLLERR | POLLHUP | POLLNVAL))
			break;
		if (!(tPollFd.revents & POLLIN))
			continue;

		iClientFd = accept(ptServer->iListenFd, NULL, NULL);
		if (iClientFd < 0)
			continue;
		if (SetClientTimeout(iClientFd) < 0)
		{
			close(iClientFd);
			continue;
		}

		pthread_mutex_lock(&ptServer->tLock);
		if (ptServer->iStop)
		{
			pthread_mutex_unlock(&ptServer->tLock);
			close(iClientFd);
			break;
		}
		ptClient = FindFreeClientLocked(ptServer);
		if (ptClient == NULL)
		{
			pthread_mutex_unlock(&ptServer->tLock);
			RejectBusyClient(iClientFd);
			continue;
		}
		ptClient->iFd = iClientFd;
		ptClient->iActive = 1;
		iRet = pthread_create(&ptClient->tThread, NULL,
				      ClientThread, ptClient);
		if (iRet == 0)
			ptClient->iThreadStarted = 1;
		else
		{
			ptClient->iFd = -1;
			ptClient->iActive = 0;
		}
		pthread_mutex_unlock(&ptServer->tLock);
		if (iRet != 0)
		{
			fprintf(stderr, "pthread_create client: %s\n", strerror(iRet));
			close(iClientFd);
		}
	}
	ReapFinishedClients(ptServer);
	return NULL;
}

int HttpServerInit(struct http_server *ptServer, unsigned short iPort,
		   struct web_command *ptWebCommand)
{
	struct sockaddr_in tAddress;
	int iReuse = 1;
	int iFlags;
	int iRet;
	int i;

	memset(ptServer, 0, sizeof(*ptServer));
	ptServer->iListenFd = -1;
	ptServer->ptWebCommand = ptWebCommand;
	for (i = 0; i < HTTP_MAX_CLIENTS; i++)
	{
		ptServer->atClients[i].ptServer = ptServer;
		ptServer->atClients[i].iFd = -1;
		ptServer->atClients[i].iIndex = i;
	}
	iRet = pthread_mutex_init(&ptServer->tLock, NULL);
	if (iRet != 0)
	{
		fprintf(stderr, "pthread_mutex_init: %s\n", strerror(iRet));
		return -1;
	}
	iRet = pthread_cond_init(&ptServer->tFrameReady, NULL);
	if (iRet != 0)
	{
		fprintf(stderr, "pthread_cond_init: %s\n", strerror(iRet));
		pthread_mutex_destroy(&ptServer->tLock);
		return -1;
	}

	ptServer->iListenFd = socket(AF_INET, SOCK_STREAM, 0);
	if (ptServer->iListenFd < 0)
		goto err_exit;
	setsockopt(ptServer->iListenFd, SOL_SOCKET, SO_REUSEADDR,
		   &iReuse, sizeof(iReuse));
	memset(&tAddress, 0, sizeof(tAddress));
	tAddress.sin_family = AF_INET;
	tAddress.sin_addr.s_addr = htonl(INADDR_ANY);
	tAddress.sin_port = htons(iPort);
	if (bind(ptServer->iListenFd, (struct sockaddr *)&tAddress,
		 sizeof(tAddress)) < 0 ||
	    listen(ptServer->iListenFd, HTTP_MAX_CLIENTS) < 0)
		goto err_exit;
	iFlags = fcntl(ptServer->iListenFd, F_GETFL);
	if (iFlags < 0 ||
	    fcntl(ptServer->iListenFd, F_SETFL, iFlags | O_NONBLOCK) < 0)
		goto err_exit;
	iRet = pthread_create(&ptServer->tAcceptThread, NULL,
			      AcceptThread, ptServer);
	if (iRet != 0)
	{
		fprintf(stderr, "pthread_create accept: %s\n", strerror(iRet));
		goto err_exit_without_perror;
	}
	ptServer->iAcceptThreadStarted = 1;
	printf("Camera web server: http://<board-ip>:%u/\n", (unsigned int)iPort);
	return 0;

err_exit:
	perror("initialize HTTP server");
err_exit_without_perror:
	HttpServerExit(ptServer);
	return -1;
}

int HttpServerPublish(struct http_server *ptServer,
		      const unsigned char *pucJpeg, unsigned int iLength)
{
	unsigned char *pucNew;

	if (iLength == 0)
		return -1;
	pthread_mutex_lock(&ptServer->tLock);
	if (ptServer->iStop)
	{
		pthread_mutex_unlock(&ptServer->tLock);
		return -1;
	}
	if (ptServer->iCapacity < iLength)
	{
		pucNew = realloc(ptServer->pucLatestFrame, iLength);
		if (pucNew == NULL)
		{
			pthread_mutex_unlock(&ptServer->tLock);
			return -1;
		}
		ptServer->pucLatestFrame = pucNew;
		ptServer->iCapacity = iLength;
	}
	memcpy(ptServer->pucLatestFrame, pucJpeg, iLength);
	ptServer->iFrameLength = iLength;
	ptServer->iGeneration++;
	pthread_cond_broadcast(&ptServer->tFrameReady);
	pthread_mutex_unlock(&ptServer->tLock);
	return 0;
}

void HttpServerExit(struct http_server *ptServer)
{
	pthread_t tThread;
	int iJoin;
	int i;

	pthread_mutex_lock(&ptServer->tLock);
	ptServer->iStop = 1;
	for (i = 0; i < HTTP_MAX_CLIENTS; i++)
	{
		if (ptServer->atClients[i].iFd >= 0)
			shutdown(ptServer->atClients[i].iFd, SHUT_RDWR);
	}
	pthread_cond_broadcast(&ptServer->tFrameReady);
	pthread_mutex_unlock(&ptServer->tLock);
	if (ptServer->iListenFd >= 0)
		shutdown(ptServer->iListenFd, SHUT_RDWR);
	if (ptServer->iAcceptThreadStarted)
		pthread_join(ptServer->tAcceptThread, NULL);

	for (i = 0; i < HTTP_MAX_CLIENTS; i++)
	{
		pthread_mutex_lock(&ptServer->tLock);
		iJoin = ptServer->atClients[i].iThreadStarted;
		if (iJoin)
		{
			tThread = ptServer->atClients[i].tThread;
			ptServer->atClients[i].iThreadStarted = 0;
		}
		pthread_mutex_unlock(&ptServer->tLock);
		if (iJoin)
			pthread_join(tThread, NULL);
	}
	if (ptServer->iListenFd >= 0)
		close(ptServer->iListenFd);
	free(ptServer->pucLatestFrame);
	pthread_cond_destroy(&ptServer->tFrameReady);
	pthread_mutex_destroy(&ptServer->tLock);
}
