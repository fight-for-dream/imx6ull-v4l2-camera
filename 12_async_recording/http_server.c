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
	int iStop;

	/* 浏览器请求 /stream；其它路径直接返回 404。 */
	/* TCP 可能分段，读到完整请求行后再判断路径。 */
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
		/* 等待主线程提供新帧，只复制最新的一帧。 */
		pthread_mutex_lock(&ptServer->tLock);
		while (!ptServer->iStop &&
		       ptServer->iGeneration == iLastGeneration)
			pthread_cond_wait(&ptServer->tFrameReady, &ptServer->tLock);
		iStop = ptServer->iStop;
		if (iStop)
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

		/* 一个 multipart 部分就是一帧完整的 JPEG。 */
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

static void *ServerThread(void *pArgument)
{
	struct http_server *ptServer = pArgument;
	struct pollfd tPollFd;
	struct timeval tTimeout;
	int iClientFd;
	int iRet;

	tTimeout.tv_sec = 2;
	tTimeout.tv_usec = 0;
	while (!ServerStopped(ptServer))
	{
		tPollFd.fd = ptServer->iListenFd;
		tPollFd.events = POLLIN;
		tPollFd.revents = 0;
		iRet = poll(&tPollFd, 1, 500);
		if (iRet < 0 && errno == EINTR)
			continue;
		if (iRet < 0)
		{
			perror("poll HTTP socket");
			break;
		}
		if (iRet == 0 || !(tPollFd.revents & POLLIN))
		{
			if (tPollFd.revents & (POLLERR | POLLHUP | POLLNVAL))
				break;
			continue;
		}
		iClientFd = accept(ptServer->iListenFd, NULL, NULL);
		if (iClientFd < 0)
		{
			if (errno != EAGAIN && errno != EINTR)
				perror("accept HTTP client");
			continue;
		}
		if (setsockopt(iClientFd, SOL_SOCKET, SO_RCVTIMEO,
			       &tTimeout, sizeof(tTimeout)) < 0 ||
		    setsockopt(iClientFd, SOL_SOCKET, SO_SNDTIMEO,
			       &tTimeout, sizeof(tTimeout)) < 0)
		{
			perror("set HTTP socket timeout");
			close(iClientFd);
			continue;
		}
		pthread_mutex_lock(&ptServer->tLock);
		ptServer->iClientFd = iClientFd;
		pthread_mutex_unlock(&ptServer->tLock);
		printf("HTTP client connected.\n");
		StreamClient(ptServer, iClientFd);
		pthread_mutex_lock(&ptServer->tLock);
		ptServer->iClientFd = -1;
		pthread_mutex_unlock(&ptServer->tLock);
		close(iClientFd);
		printf("HTTP client disconnected.\n");
	}
	return NULL;
}

int HttpServerInit(struct http_server *ptServer, unsigned short iPort)
{
	struct sockaddr_in tAddress;
	int iReuse = 1;
	int iFlags;
	int iRet;

	memset(ptServer, 0, sizeof(*ptServer));
	ptServer->iListenFd = -1;
	ptServer->iClientFd = -1;
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
	if (listen(ptServer->iListenFd, 1) < 0)
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
	iRet = pthread_create(&ptServer->tThread, NULL, ServerThread, ptServer);
	if (iRet != 0)
	{
		fprintf(stderr, "pthread_create: %s\n", strerror(iRet));
		goto err_exit;
	}
	ptServer->iThreadStarted = 1;
	printf("MJPG HTTP server: port %u, path /stream\n",
	       (unsigned int)iPort);
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
	pthread_cond_signal(&ptServer->tFrameReady);
	pthread_mutex_unlock(&ptServer->tLock);
	return 0;
}

void HttpServerExit(struct http_server *ptServer)
{
	int iClientFd;

	pthread_mutex_lock(&ptServer->tLock);
	ptServer->iStop = 1;
	iClientFd = ptServer->iClientFd;
	pthread_cond_broadcast(&ptServer->tFrameReady);
	pthread_mutex_unlock(&ptServer->tLock);
	if (iClientFd >= 0)
		shutdown(iClientFd, SHUT_RDWR);
	if (ptServer->iThreadStarted)
		pthread_join(ptServer->tThread, NULL);
	if (ptServer->iListenFd >= 0)
		close(ptServer->iListenFd);
	free(ptServer->pucLatestFrame);
	pthread_cond_destroy(&ptServer->tFrameReady);
	pthread_mutex_destroy(&ptServer->tLock);
	ptServer->iListenFd = -1;
}
