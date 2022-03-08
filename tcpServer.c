#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <pthread.h>
#include <sys/select.h>
#include <sys/poll.h>
#include <sys/epoll.h>

#define THREAD 1
#define SELECT 2
#define POLL 3
#define EPOLL 4
#define TYPE EPOLL



#define MAXLNE 4096-1
#define POLL_SIZE 1024
#define PORT 9999

void* client_routine(void* arg) 
{
	int connfd = *(int*)arg;

	char buff[MAXLNE+1];
	while (1)
	{
		int n = recv(connfd, buff, MAXLNE, 0);
		if (n > 0)
		{
			buff[n] = '\0';
			printf("recv ms from client:%s\n", buff);

			send(connfd, buff, n, 0);
		}
		else if (n == 0)
		{
			printf("disconnected\n");
			close(connfd);
			break;
		}
	}
	return NULL;
}

int main(int argc, char** argv)
{
	int listenfd, connfd ,n;
	struct sockaddr_in serveraddr;
	char buff[MAXLNE+1];

	if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
	{
		printf("create socket error:%s(errno:%d)\n", strerror(errno), errno);
		return 0;
	}
	memset(&serveraddr, 0, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = htons(INADDR_ANY);
	serveraddr.sin_port = htons(PORT);

	if (bind(listenfd, (struct sockaddr*)&serveraddr, sizeof(serveraddr)) == -1)
	{
		printf("bind socket error:%s(errno:%d)\n", strerror(errno), errno);
		return 0;
	}

	if (listen(listenfd, 10) == -1)
	{
		printf("listen socket error:%s(errno:%d)\n", strerror(errno), errno);
		return 0;
	}


#if TYPE==THREAD
	while (1)
	{
		struct sockaddr_in client;
		socklen_t len= sizeof(client);
		if ((connfd = accept(listenfd, (struct sockaddr*)&client, &len)) == -1)
		{
			printf("aaaaaaaa\n");
			printf("accept socket error: %s(errno: %d)\n", strerror(errno), errno);
			break;
		}

		pthread_t threadid;
		pthread_create(&threadid, NULL, client_routine, (void*)&connfd);
	}

#elif TYPE==SELECT
	fd_set rfds, rset, wfds, wset;
	FD_ZERO(&rfds);
	FD_ZERO(&wfds);

	FD_SET(listenfd, &rfds);

	int max_fd = listenfd;

	while (1)
	{
		rset = rfds;
		wset = wfds;

		int nready = select(max_fd + 1, &rset, &wset, NULL, NULL);

		if (FD_ISSET(listenfd, &rset))
		{
			struct sockaddr_in client;
			socklen_t len = sizeof(client);
			if ((connfd = accept(listenfd, (struct sockaddr*)&client, &len)) == -1)
			{
				printf("accept socket error: %s(errno: %d)\n", strerror(errno), errno);
				break;
			}
			FD_SET(connfd, &rfds);

			if (connfd > max_fd) max_fd = connfd;

			if (--nready == 0) continue;
		}

		int i = 0;
		for (i = listenfd+1; i <= max_fd; i++)
		{
			if (FD_ISSET(i, &rset))
			{
				n = recv(i, buff, MAXLNE, 0);
				if (n > 0)
				{
					buff[n] = '\0';
					printf("recv ms from client(%d):%s\n", i,buff);

					//此处应该将buff存储到该client对应的缓存区，以备后续调用

					FD_SET(i, &wfds);
					
				}
				else if (n == 0)
				{
					printf("client(%d):disconnected\n", i);
					FD_CLR(i, &rfds);
					close(i);
				}
				if (--nready == 0) break;

			}		
			if (FD_ISSET(i, &wset))  
			{
				//这儿将要发送给该客户端的buff取出，并设置长度n

				send(i, buff, n, 0);
				FD_CLR(i, &wfds);
				FD_SET(i, &rfds);
				if (--nready == 0) break;
			}
		}
	}

#elif TYPE==POLL
	struct pollfd fds[POLL_SIZE] = { 0 };
	for (int i = 0; i < POLL_SIZE; ++i)
	{
		fds[i].fd = -1;
	}

	fds[listenfd].fd = listenfd;
	fds[listenfd].events = POLLIN;

	int max_fd = listenfd;
	while (1)
	{
		int nready = poll(fds, max_fd + 1, -1);

		if (fds[listenfd].revents & POLLIN)
		{
			struct sockaddr_in client;
			socklen_t len = sizeof(client);
			if ((connfd = accept(listenfd, (struct sockaddr*)&client, &len)) == -1)
			{
				printf("accept socket error: %s(errno: %d)\n", strerror(errno), errno);
				break;
			}
			fds[connfd].fd = connfd;
			fds[connfd].events = POLLIN;

			if (connfd > max_fd) max_fd = connfd;

			if (--nready == 0) continue;
		}

		for (int i = listenfd + 1; i <= max_fd; ++i)
		{
			if (fds[i].revents & POLLIN)
			{
				n = recv(i, buff, MAXLNE, 0);
				if (n > 0)
				{
					buff[n] = '\0';
					printf("recv ms from client(%d):%s\n", i, buff);

					//一般不直接在此处回应客户端，此处为了理解方便才这么些
					send(i, buff, n, 0);
				}
				else if (n == 0)
				{
					fds[i].fd = -1;
					printf("client(%d):disconnected\n", i);
					close(i);
				}
				if (--nready == 0) break;
			}

		}
	}

#elif TYPE==EPOLL

	//epoll_create
	//epoll_ctl() ADD,DEL,MOD
	//epoll_wait

	int epfd = epoll_create(1);  //此处的1没有实际意义，只要大于0即可，只是为了兼容前置版本的形	参

	struct epoll_event events[POLL_SIZE] = { 0 };  //epoll的最大连接数并没有限制，此处的大小限制是一次最多处理的最大接连数
	struct epoll_event ev;

	ev.events = EPOLLIN;
	ev.data.fd = listenfd;

	epoll_ctl(epfd, EPOLL_CTL_ADD, listenfd, &ev);

	while (1)
	{
		int nready = epoll_wait(epfd, events, POLL_SIZE, 5);
		if (nready == -1)
		{
			continue;
		}

		for (int i = 0; i < nready; ++i)
		{
			int clientfd = events[i].data.fd;
			if (clientfd == listenfd)
			{
				struct sockaddr_in client;
				socklen_t len = sizeof(client);
				if ((connfd = accept(listenfd, (struct sockaddr*)&client, &len)) == -1)
				{
					printf("accept socket error: %s(errno: %d)\n", strerror(errno), errno);
					return 0;
				}

				ev.events = EPOLLIN;
				ev.data.fd = connfd;
				epoll_ctl(epfd, EPOLL_CTL_ADD, connfd, &ev);
			}
			else if (events[i].events & EPOLLIN)
			{
				n = recv(clientfd, buff, MAXLNE, 0);
				if (n > 0)
				{
					buff[n] = '\0';
					printf("recv ms from client(%d):%s\n", clientfd, buff);

					//一般不直接在此处回应客户端，此处为了理解方便才这么些
					send(clientfd, buff, n, 0);
				}
				else if (n == 0)
				{
					ev.events = EPOLLIN;
					ev.data.fd = clientfd;

					epoll_ctl(epfd, EPOLL_CTL_DEL, clientfd, &ev);

					printf("client(%d):disconnected\n", clientfd);

					close(clientfd);
				}
			}
		}
	}


#endif

	close(listenfd);
	return 0;
}






