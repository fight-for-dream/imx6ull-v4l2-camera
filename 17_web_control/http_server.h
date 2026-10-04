#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include <pthread.h>
#include <stddef.h>

#define HTTP_MAX_CLIENTS 4

struct http_server;
struct web_command;

struct http_client
{
	pthread_t tThread;
	struct http_server *ptServer;
	int iFd;
	int iIndex;
	int iActive;
	int iThreadStarted;
};

/* 网络状态全部封装在HTTP模块中，摄像头主循环不管理客户端。 */
struct http_server
{
	pthread_t tAcceptThread;
	pthread_mutex_t tLock;
	pthread_cond_t tFrameReady;
	struct http_client atClients[HTTP_MAX_CLIENTS];
	struct web_command *ptWebCommand;
	unsigned char *pucLatestFrame;
	size_t iCapacity;
	size_t iFrameLength;
	unsigned int iGeneration;
	int iListenFd;
	int iStop;
	int iAcceptThreadStarted;
};

int HttpServerInit(struct http_server *ptServer, unsigned short iPort,
		   struct web_command *ptWebCommand);
int HttpServerPublish(struct http_server *ptServer,
		      const unsigned char *pucJpeg, unsigned int iLength);
void HttpServerExit(struct http_server *ptServer);

#endif
