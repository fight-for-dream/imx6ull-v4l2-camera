#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include <pthread.h>
#include <stddef.h>

/* 只保留最新一帧；网络慢时旧帧会被覆盖，不拖慢摄像头。 */
struct http_server
{
	pthread_t tThread;
	pthread_mutex_t tLock;
	pthread_cond_t tFrameReady;
	unsigned char *pucLatestFrame;
	size_t iCapacity;
	size_t iFrameLength;
	unsigned int iGeneration;
	int iListenFd;
	int iClientFd;
	int iStop;
	int iThreadStarted;
};

int HttpServerInit(struct http_server *ptServer, unsigned short iPort);
int HttpServerPublish(struct http_server *ptServer,
		      const unsigned char *pucJpeg, unsigned int iLength);
void HttpServerExit(struct http_server *ptServer);

#endif
