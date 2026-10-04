#include "http_server.h"

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

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

static void StreamClient(struct http_server *ptServer, int iClientFd)
{
	static const char acHeader[] =
		"HTTP/1.1 200 OK\r\n"
		"Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
		"Cache-Control: no-cache\r\n"
		"Connection: close\r\n\r\n";
	static const char acNotFound[] =
		"HTTP/1.1 404 Not Found\r\n"
		"Content-Length: 0\r\n"
		"Connection: close\r\n\r\n";
	unsigned char *pucFrame = NULL;
	unsigned char *pucNew;
	size_t iCapacity = 0;
	size_t iLength;
	size_t iRequestLength = 0;
	unsigned int iLastGeneration = 0;
	char acRequest[256];
	char acPart[128];
	ssize_t iRead;
	int iPartLength;

	/* TCP可能分段，先读取完整的HTTP请求行。 */
	do
	{
		iRead = recv(iClientFd, acRequest + iRequestLength,
			     sizeof(acRequest) - 1 - iRequestLength, 0);
		if (iRead <= 0)
			return;
		iRequestLength += iRead;
		acRequest[iRequestLength] = '\0';
	} while (strstr(acRequest, "\r\n") == NULL &&
		 iRequestLength < sizeof(acRequest) - 1);

	if (strncmp(acRequest, "GET /stream ", 12) != 0)
	{
		SendAll(iClientFd, acNotFound, sizeof(acNotFound) - 1);
		return;
	}
	if (SendAll(iClientFd, acHeader, sizeof(acHeader) - 1) < 0)
		return;

	while (1)
	{
		/* 每个客户端独立记录自己已经发送到哪一帧。 */
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

static void *ClientThread(void *pArgument)
{
	struct http_client *ptClient = pArgument;
	struct http_server *ptServer = ptClient->ptServer;
	int iClientFd = ptClient->iFd;
	int iActiveCount;

	pthread_mutex_lock(&ptServer->tLock);
	iActiveCount = ActiveClientCountLocked(ptServer);
	pthread_mutex_unlock(&ptServer->tLock);
	printf("HTTP client %d connected (%d active).\n",
	       ptClient->iIndex + 1, iActiveCount);

	StreamClient(ptServer, iClientFd);

	/* 先从共享表中移除fd，再关闭，避免退出线程使用陈旧fd。 */
	pthread_mutex_lock(&ptServer->tLock);
	ptClient->iFd = -1;
	ptClient->iActive = 0;
	iActiveCount = ActiveClientCountLocked(ptServer);
	pthread_mutex_unlock(&ptServer->tLock);
	close(iClientFd);
	printf("HTTP client %d disconnected (%d active).\n",
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
	{
		perror("set HTTP socket timeout");
		return -1;
	}
	return 0;
}

static void RejectBusyClient(int iClientFd)
{
	static const char acBusy[] =
		"HTTP/1.1 503 Service Unavailable\r\n"
		"Content-Length: 0\r\n"
		"Connection: close\r\n\r\n";

	SendAll(iClientFd, acBusy, sizeof(acBusy) - 1);
	close(iClientFd);
	fprintf(stderr, "HTTP client rejected: maximum %d clients.\n",
		HTTP_MAX_CLIENTS);
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
		{
			if (errno != EAGAIN && errno != EINTR &&
			    !ServerStopped(ptServer))
				perror("accept HTTP client");
			continue;
		}
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

int HttpServerInit(struct http_server *ptServer, unsigned short iPort)
{
	struct sockaddr_in tAddress;
	int iReuse = 1;
	int iFlags;
	int iRet;
	int i;

	memset(ptServer, 0, sizeof(*ptServer));
	ptServer->iListenFd = -1;
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
	{
		perror("socket");
		goto err_exit;
	}
	setsockopt(ptServer->iListenFd, SOL_SOCKET, SO_REUSEADDR,
		   &iReuse, sizeof(iReuse));
	memset(&tAddress, 0, sizeof(tAddress));
	tAddress.sin_family = AF_INET;
	tAddress.sin_addr.s_addr = htonl(INADDR_ANY);
	tAddress.sin_port = htons(iPort);
	if (bind(ptServer->iListenFd, (struct sockaddr *)&tAddress,
		 sizeof(tAddress)) < 0)
	{
		perror("bind HTTP port");
		goto err_exit;
	}
	if (listen(ptServer->iListenFd, HTTP_MAX_CLIENTS) < 0)
	{
		perror("listen");
		goto err_exit;
	}
	iFlags = fcntl(ptServer->iListenFd, F_GETFL);
	if (iFlags < 0 ||
	    fcntl(ptServer->iListenFd, F_SETFL, iFlags | O_NONBLOCK) < 0)
	{
		perror("fcntl HTTP socket");
		goto err_exit;
	}

	iRet = pthread_create(&ptServer->tAcceptThread, NULL,
			      AcceptThread, ptServer);
	if (iRet != 0)
	{
		fprintf(stderr, "pthread_create accept: %s\n", strerror(iRet));
		goto err_exit;
	}
	ptServer->iAcceptThreadStarted = 1;
	printf("MJPG HTTP server: port %u, path /stream, max %d clients\n",
	       (unsigned int)iPort, HTTP_MAX_CLIENTS);
	return 0;

err_exit:
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
			fprintf(stderr, "Latest frame allocation failed.\n");
			return -1;
		}
		ptServer->pucLatestFrame = pucNew;
		ptServer->iCapacity = iLength;
	}
	memcpy(ptServer->pucLatestFrame, pucJpeg, iLength);
	ptServer->iFrameLength = iLength;
	ptServer->iGeneration++;
	/* 多个视频客户端都需要收到新帧通知。 */
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

	/* accept线程退出后，不会再创建或回收客户端线程。 */
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
	ptServer->iListenFd = -1;
}
