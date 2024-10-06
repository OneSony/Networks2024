#include <sys/socket.h>
#include <netinet/in.h>

#include <unistd.h>
#include <errno.h>

#include <string.h>
#include <memory.h>
#include <stdio.h>

#define SENTENCE_LEN 10000

enum user_status {
	CONNECTED,
	USER,
	PASS,
	PORT,
	PASV
};

int connect_to(int* sockfd, char* ip, int port) {
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

int listen_at(int* sockfd, int port) {
	if ((*sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
		printf("Error socket(): %s(%d)\n", strerror(errno), errno);
		return 1;
	}

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	if (bind(*sockfd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
		printf("Error bind(): %s(%d)\n", strerror(errno), errno);
		close(*sockfd);
		return 1;
	}

	if (listen(*sockfd, 5) == -1) {
		printf("Error listen(): %s(%d)\n", strerror(errno), errno);
		close(*sockfd);
		return 1;
	}

	return 0;
}


int get_response(int sockfd) { //TODO
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

	printf("FROM SERVER: %s\n", sentence);
	return 0;
}

int main(int argc, char **argv) {
	int sockfd;
	struct sockaddr_in addr;
	char sentence[8192];
	int len;
	int p;

	if(-1==connect_to(&sockfd, "127.0.0.1", 6789)){
		return 1;
	}

	get_response(sockfd);

	listen(&sockfd, 6789);

	write(sockfd, "POST ", );

	while(1){

		/*
		//获取键盘输入
		fgets(sentence, 4096, stdin);
		len = strlen(sentence);
		sentence[len] = '\r';
		sentence[len+1] = '\n';
		sentence[len + 2] = '\0';

		
		//把键盘输入写入socket
		p = 0;
		while (p < len) {
			int n = write(sockfd, sentence + p, len + 2 - p);		//write函数不保证所有的数据写完，可能中途退出
			if (n < 0) {
				printf("Error write(): %s(%d)\n", strerror(errno), errno);
				return 1;
			} else {
				p += n;
			}			
		}

		printf("TO SERVER: %s", sentence);

		//榨干socket接收到的内容
		get_response(sockfd);		//注意：read并不会将字符串加上'\0'，需要手动添加
		*/

	}

	return 0;
}
