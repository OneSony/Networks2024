#include <sys/socket.h>
#include <netinet/in.h>

#include <unistd.h>
#include <errno.h>

#include <ctype.h>
#include <string.h>
#include <memory.h>
#include <stdio.h>

#define SENTENCE_LEN 8192

enum user_status {
	CONNECTED,
	USER,
	PASS,
	PORT,
	PASV
};

enum user_status status; 

struct request{
	char verb[5];
	char parameter[256];
};

int connect_ip(int* sockfd, char* ip, int port) {
	if ((*sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
		printf("Error socket(): %s(%d)\n", strerror(errno), errno);
		return 1;
	}

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = inet_addr(ip);

	if (connect(*sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
		printf("Error connect(): %s(%d)\n", strerror(errno), errno);
		close(*sockfd);
		return 1;
	}

	return 0;
}

int get_request(int sockfd, struct request* req) {
	char sentence[SENTENCE_LEN];
	int p = 0;
	while (1) {
		int n = read(sockfd, sentence + p, SENTENCE_LEN - p);
		if (n < 0) {
			printf("Error read(): %s(%d)\n", strerror(errno), errno);
			return -1;
		} else if (n == 0) {
			break; //TODO
		} else {
			p += n;
			if (sentence[p - 2] == '\r' && sentence[p - 1] == '\n') {
				sentence[p - 2] = '\0';
				break;
			}
		}
	}

	sscanf(sentence,"%s %[^\n]",req->verb,req->parameter);
	return 0;
}

int send_response(int sockfd, char* sentence) {
	int len = strlen(sentence);
	int p = 0;
	while (p < len) {
		int n = write(sockfd, sentence + p, len - p);
		if (n < 0) {
			printf("Error write(): %s(%d)\n", strerror(errno), errno);
			return -1;
		} else {
			p += n;
		}
	}
	return 0;
}

int handle_request(int sockfd, struct request* req) {

	if(status==CONNECTED){
		if (strcmp(req->verb, "USER") == 0 && strcmp(req->parameter, "anonymous") == 0) {
			status=USER;
			send_response(sockfd, "331 Please specify the password.\r\n");//ask for an email address TODO
		}else{
			send_response(sockfd, "wrong\r\n"); //TODO
		}
	}else if(status==USER){
		if (strcmp(req->verb, "PASS") == 0) {
			status=PASS;
			send_response(sockfd, "230 Login successful.\r\n");
		}else{
			send_response(sockfd, "wrong\r\n"); //TODO
		}
	}else if(status==PASS){
		if (strcmp(req->verb, "PORT") == 0) {
			int p1,p2,p3,p4,p5,p6;
			sscanf(req->parameter,"%d,%d,%d,%d,%d,%d",&p1,&p2,&p3,&p4,&p5,&p6);

			char ip[16];
			sprintf(ip,"%d.%d.%d.%d",p1,p2,p3,p4);

			int port=p5*256+p6;

			int datafd;
			if(-1==connect_ip(&datafd,ip,port)){
				printf("connect_ip error\n");
			}
			printf("connect_ip success\n");
			status=PORT;
			
		} else if (strcmp(req->verb, "PASV") == 0) {
			status=PASV;
			send_response(sockfd, "227 Entering Passive Mode (127,0,0,1,0,20).\r\n");
		}
	}
	return 0;
}



int main(int argc, char **argv) {
	int listenfd, connfd;		//监听socket和连接socket不一样，后者用于数据传输
	struct sockaddr_in addr;
	char sentence[8192];
	int p;
	int len;

	//创建socket
	if ((listenfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
		printf("Error socket(): %s(%d)\n", strerror(errno), errno);
		return 1;
	}

	//设置本机的ip和port
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = 6789;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);	//监听"0.0.0.0"

	//将本机的ip和port与socket绑定
	if (bind(listenfd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
		printf("Error bind(): %s(%d)\n", strerror(errno), errno);
		return 1;
	}

	//开始监听socket
	if (listen(listenfd, 10) == -1) {
		printf("Error listen(): %s(%d)\n", strerror(errno), errno);
		return 1;
	}

	//持续监听连接请求
	while (1) {
		//等待client的连接 -- 阻塞函数
		if ((connfd = accept(listenfd, NULL, NULL)) == -1) {
			printf("Error accept(): %s(%d)\n", strerror(errno), errno);
			continue;
		}

		//fork一个子进程处理连接
		int pid = fork();
        if (pid < 0) {

        } else if (pid == 0) {
			status=CONNECTED;
            close(listenfd);
			send_response(connfd, "220 Anonymous FTP server ready.\r\n");
			while (1){
				struct request req;
				get_request(connfd,&req);
				printf("verb: %s\n",req.verb);
				printf("parameter: %s\n",req.parameter);

				//解析
				if(handle_request(connfd,&req)==1){
					break;
				}
			}
			close(connfd);
			return 0;
		}
	}

	close(listenfd);
}

