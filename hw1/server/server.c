#include <sys/socket.h>
#include <netinet/in.h>

#include <string.h>
#include <stdio.h>

#include <memory.h>

#include <errno.h>

#include <signal.h>
#include <unistd.h>

#define SENTENCE_LEN 8192
#define MIN_PORT 20000
#define MAX_PORT 65535
#define MAX_RES 100

enum user_status {
    CONNECTED,
    USER,
    PASS,
    PORT,
    PASV,
	RERT,
	STOR
};

enum user_status status;

struct request{
	char verb[5];
	char parameter[256];
};

struct response{
	char code[4];
	char message[MAX_RES][256];
};

int control_listen_socket;
int control_socket;
int data_listen_socket;
int data_socket;

int data_pid;

struct port_mode_info_s{
	char ip[16];
	int port;
};

struct pasv_mode_info_s{
	int port;
};

struct port_mode_info_s port_mode_info;
struct pasv_mode_info_s pasv_mode_info;

int get_cwd(char* str){
	if (getcwd(str, 256) == NULL) {
		printf("Error getcwd(): %s(%d)\n", strerror(errno), errno);
		return 1;
	}

	char result[1024];  // Adjust size as needed, larger than the expected string
    int j = 0;  // Index for the new buffer

    for (int i = 0; i < strlen(str); i++) {
        // Check if current character is a double quote
        if (str[i] == '"') {
            // Replace with two double quotes
            result[j++] = '"';
            result[j++] = '"';
        }
        // Check if current character is a newline (octal \012 or '\n')
        else if (str[i] == '\n') {
            // Replace newline with null character \000 (octal)
            result[j++] = '\0';
        }
        // Otherwise, copy the character as is
        else {
            result[j++] = str[i];
        }
    }

    // Add null terminator at the end of the result string
    result[j] = '\0';

    // Copy the processed string back to the original pointer
    strcpy(str, result);


}

int get_msg(int sockfd, char* sentence) {

	int p = 0;
	while (1) {
		int n = read(sockfd, sentence + p, SENTENCE_LEN - p);
		if (n < 0) {
			printf("Error read(): %s(%d)\n", strerror(errno), errno);
			return -1;
		} else if (n == 0) {
			return -1;
		} else {
			p += n;
			if (sentence[p - 2] == '\r' && sentence[p - 1] == '\n') {
				sentence[p - 2] = '\0';
				break;
			}
		}
	}

	return 0;
}

int send_msg(int sockfd, char* sentence) {
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

int send_file(int sockfd, char* filename) {
	FILE* file = fopen(filename, "rb");
	if (file == NULL) {
		printf("Error fopen(): %s(%d)\n", strerror(errno), errno);
		return 1;
	}

	char buff[256];
	int n;
	while ((n = fread(buff, 1, 256, file)) > 0) {
		printf("n: %d\n", n);
		write(sockfd, buff, n);
	}

	printf("send file success\n");

	fclose(file);
	return 0;
}

int get_file(int sockfd, char* filename) {
	FILE* file = fopen(filename, "wb");
	if (file == NULL) {
		printf("Error fopen(): %s(%d)\n", strerror(errno), errno);
		return 1;
	}

	char buff[256];
	int n;
	while ((n = read(sockfd, buff, 256)) > 0) {
		fwrite(buff, 1, n, file);
	}

	fclose(file);
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
		if (errno == EADDRINUSE) {
            printf("Error bind(): Port %d is already in use.\n", port);
            close(*sockfd);
            return 2; // 返回2表示端口已被占用
        } else {
            printf("Error bind(): %s(%d)\n", strerror(errno), errno);
            close(*sockfd);
            return 1; // 返回1表示绑定失败
        }
	}

	if (listen(*sockfd, 5) == -1) {
		printf("Error listen(): %s(%d)\n", strerror(errno), errno);
		close(*sockfd);
		return 1;
	}

	return 0;
}

int parse_response(char* msg, struct response* res) {
	memset(res, 0, sizeof(res));
	int i = 0;
	char* p = msg;
	while (1) {
		if (sscanf(p, "%*[^-]-%[^\r\n]\r\n", res->message[i]) != 1) {
			break;
		}
		p = strchr(p, '\n') + 1;
		i++;
	}
	sscanf(p, "%[^ ] %[^\r\n]\r\n", res->code, res->message[i]);
	return 0;
}

int parse_request(char* msg, struct request* req) {
	memset(req, 0, sizeof(req));
	sscanf(msg, "%s %[^\n]", req->verb, req->parameter);
	return 0;
}


int handle_request(char* msg) {

	struct request req;
	parse_request(msg, &req);
	printf("req.verb: %s\n", req.verb);
	printf("req.parameter: %s\n", req.parameter);

	if(strcmp(req.verb, "QUIT") == 0 || strcmp(req.verb, "ABOR") == 0){
		send_msg(control_socket, "221 Goodbye.\r\n");
		return 1;
	}else if(strcmp(req.verb, "USER") == 0){
		if(status==CONNECTED){
			if(strcmp(req.parameter, "anonymous") == 0){
				send_msg(control_socket, "331 Please specify the password.\r\n");//ask for an email address TODO
				status=USER;
				return 0;
			}
		}
	}else if(strcmp(req.verb, "PASS") == 0){
		if(status==USER){
			send_msg(control_socket, "230 Login successful.\r\n");
			status=PASS;

			return 0;
		}
	}else if(strcmp(req.verb, "PORT") == 0){
		if(status==PASS){
			int p1,p2,p3,p4,p5,p6;
			sscanf(req.parameter,"%d,%d,%d,%d,%d,%d",&p1,&p2,&p3,&p4,&p5,&p6);

			sprintf(port_mode_info.ip,"%d.%d.%d.%d",p1,p2,p3,p4);

			port_mode_info.port=p5*256+p6;

			send_msg(control_socket, "200 PORT command successful.\r\n");
			status=PORT;

			return 0;
		}
	}else if(strcmp(req.verb, "PASV") == 0){
		if(status==PASS){

			while(1){ //随机选一个端口
				pasv_mode_info.port = rand() % (MAX_PORT - MIN_PORT + 1) + MIN_PORT;
				if(listen_at(&data_listen_socket, pasv_mode_info.port)==0){
					break;
				}
			}
			printf("data_port: %d\n",data_listen_socket);
			char buff[256];
			sprintf(buff,"227 Entering Passive Mode (127,0,0,1,%d,%d).\r\n",pasv_mode_info.port/256,pasv_mode_info.port%256);
			send_msg(control_socket, buff);

			status=PASV;

			return 0;
		}
	}else if(strcmp(req.verb, "RETR") == 0){
		if(status==PORT){
			send_msg(control_socket, "150 Opening BINARY mode data connection for file.\r\n");

			data_pid=fork();
			if(data_pid==0){

				if(0!=connect_to(&data_socket,port_mode_info.ip,port_mode_info.port)){
					printf("connect_to error\n");
					exit(1);
				}else{
					printf("connect_ip success\n");
				}

				char filename[256];
				sscanf(req.parameter,"%s",filename);
				send_file(data_socket,filename);
				close(data_socket);
				printf("**end send file success\n");
				exit(0);
			}else{
				close(data_socket);
				int data_status;
				waitpid(data_pid, &data_status, 0);
				if(data_status==0){
					send_msg(control_socket, "226 Transfer complete.\r\n");
				}else if(data_status==1){
					send_msg(control_socket, "550 File not found.\r\n"); //TODO
				}else if(data_status==2){
					send_msg(control_socket, "500 Internal error.\r\n"); //TODO
				}else{
					send_msg(control_socket, "500 Internal error.\r\n"); //TODO
				}
				status=PASS;
			}

		}else if(status==PASV){
			send_msg(control_socket, "150 Opening BINARY mode data connection for file.\r\n");

			data_pid=fork();
			if(data_pid==0){

				//在PASV的时候已经选好了端口

				if ((data_socket = accept(data_listen_socket, NULL, NULL)) == -1) {
					printf("Error accept(): %s(%d)\n", strerror(errno), errno);
					close(data_listen_socket);
					exit(1);
				}else{
					printf("accept success\n");
				}

				char filename[256];
				sscanf(req.parameter,"%s",filename);

				send_file(data_socket,filename);

				close(data_listen_socket);
				close(data_socket);
				exit(0);
			}else{
				close(data_socket);
				int data_status;
				waitpid(data_pid, &data_status, 0);
				if(data_status==0){
					send_msg(control_socket, "226 Transfer complete.\r\n");
				}else{
					send_msg(control_socket, "550 File not found.\r\n"); //TODO
				}
				status=PASS;
			}

		}

	}else if(strcmp(req.verb, "STOR") == 0){
	}else if(strcmp(req.verb, "PWD")==0){
		if(status==PASS || status==PORT || status==PASV){
			char path[256];

			get_cwd(path);
			printf("path: %s\n",path);

			char buff[256];
			sprintf(buff,"257 \"%s\" is the current directory.\r\n",path);
			send_msg(control_socket, buff);
			return 0;
		}
	}


	send_msg(control_socket, "500 retry\r\n");
	return 2;
	
}



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

int main(){

    if(0!=listen_at(&control_listen_socket, 6789)){
		return 1;
	}
	printf("listenfd: %d\n",control_listen_socket);

    while (1) {
 
		if ((control_socket = accept(control_listen_socket, NULL, NULL)) == -1) {
			printf("Error accept(): %s(%d)\n", strerror(errno), errno);
			continue;
		}

		int pid = fork();
    	if(pid==0){
			//子进程
			printf("controlfd: %d\n",control_socket);

			status=CONNECTED;
            close(control_listen_socket);
			send_msg(control_socket, "220 Anonymous FTP server ready.\r\n");

			while (1){
				printf("in the loop!\n");
				char msg[SENTENCE_LEN];
				if(0!=get_msg(control_socket,msg)){
					printf("get_msg error\n");
					//TODO 清空port
					break;
				}

				printf("msg: %s\n",msg);

				if(handle_request(msg)==1){
					break;
				}
			}

			close(control_socket);
			return 0;
		}
	}

	close(control_listen_socket);
}