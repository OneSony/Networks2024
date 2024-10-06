#include <sys/socket.h>
#include <netinet/in.h>

#include <string.h>
#include <stdio.h>

#include <memory.h>

#include <signal.h>
#include <unistd.h>

#include <errno.h>

#define SENTENCE_LEN 8192

#define MIN_PORT 20000
#define MAX_PORT 65535
#define MAX_RES 100

enum user_status {
	UNCONNECTED,
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
	char ip[16];
	int port;
};

struct port_mode_info_s port_mode_info;
struct pasv_mode_info_s pasv_mode_info;


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


int get_msg(int sockfd, char* sentence) {

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

	printf("*response: %s\n", sentence);

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
		write(sockfd, buff, n);
	}

	fclose(file);
	return 0;
}

int get_file(int sockfd, char* filename) {
	FILE* file = fopen(filename, "wb");
	if (file == NULL) {
		printf("Error fopen(): %s(%d)\n", strerror(errno), errno);
		return 1;
	}
	printf("in\n");

	char buff[256];
	int n;
	while ((n = read(sockfd, buff, 256)) > 0) {
		printf("n: %d\n", n);
		fwrite(buff, 1, n, file);
	}

	fclose(file);
	return 0;
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



int parse_response(char* msg, struct response* res) {
	memset(res, 0, sizeof(res));
	//help me to read the response
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

int handle_request(char* msg_req) {//一直到这个请求完成

	printf("msg_req: %s\n", msg_req);

	struct request req;

	parse_request(msg_req, &req);

	char msg_res[SENTENCE_LEN];

	if(strcmp(req.verb, "USER") == 0){
		if(status==CONNECTED){
			send_msg(control_socket, msg_req);

			struct response res;
			get_msg(control_socket, msg_res);
			parse_response(msg_res, &res);
			if(strcmp(res.code, "331") == 0){
				status=USER;
			}
		}
	}else if(strcmp(req.verb, "PASS") == 0){
		if(status==USER){
			send_msg(control_socket, msg_req);

			struct response res;
			get_msg(control_socket, msg_res);
			parse_response(msg_res, &res);
			if(strcmp(res.code, "230") == 0){
				status=PASS;
			}
		}
	}else if(strcmp(req.verb, "PORT") == 0){
		if(status==PASS){
			int ip1, ip2, ip3, ip4, port1, port2;

			sscanf(req.parameter, "%d,%d,%d,%d,%d,%d", &ip1, &ip2, &ip3, &ip4, &port1, &port2);

			if(0!=listen_at(&data_listen_socket, port1 * 256 + port2)){
				return 1;
			}

			send_msg(control_socket, msg_req);
			get_msg(control_socket, msg_res);
			printf("response: %s\n", msg_res);
			status=PORT;
		}
	}else if(strcmp(req.verb, "PASV") == 0){
		if(status==PASS){
			send_msg(control_socket, msg_req);
			//TODO

			get_msg(control_socket, msg_res);

			printf("response: %s\n", msg_res);
			struct response res;
			parse_response(msg_res, &res);


			int p1,p2,p3,p4,p5,p6;
			sscanf(res.message,"%*[^(](%d,%d,%d,%d,%d,%d)%*[^\n]",&p1,&p2,&p3,&p4,&p5,&p6);

			sprintf(pasv_mode_info.ip,"%d.%d.%d.%d",p1,p2,p3,p4);
			
			pasv_mode_info.port=p5*256+p6;

			send_msg(control_socket, "200 PORT command successful.\r\n");
			status=PASV;
		}
	}else if(strcmp(req.verb, "RETR") == 0){
		if(status==PORT){
			send_msg(control_socket, msg_req);
			get_msg(control_socket, msg_res);
			printf("response: %s\n", msg_res); //150?

			int pid=fork();
			if(pid==0){//子进程
				int data_socket;
				printf("**in the child process\n");
				if ((data_socket = accept(data_listen_socket, NULL, NULL)) == -1) {
					printf("Error accept(): %s(%d)\n", strerror(errno), errno);
					close(data_listen_socket);
					return 1;
				}

				printf("data_socket: %d\n", data_socket);

				char filename[256];
				sscanf(req.parameter, "%s", filename);
				printf("filename: %s\n", filename);
				printf("get file...\n");
				get_file(data_socket, filename);
				printf("end get file\n");



				close(data_socket);
				close(data_listen_socket);
				exit(0);
			}else{
				close(data_listen_socket);
				int data_status;
				printf("**wait for the child process\n");
				waitpid(pid, &data_status, 0);
				if(data_status==0){
					
				}else{
					
				}

				get_msg(control_socket, msg_res);
				printf("response: %s\n", msg_res);

				status=PASS;
			}
		}else if(status==PASV){
			send_msg(control_socket, msg_req);
			get_msg(control_socket, msg_res);
			printf("response: %s\n", msg_res); //150?

			data_pid=fork();
			if(data_pid==0){

				if(0!=connect_to(&data_socket,pasv_mode_info.ip,pasv_mode_info.port)){
					printf("connect_to error\n");
					exit(1);
				}else{
					printf("connect_ip success\n");
				}

				char filename[256];
				sscanf(req.parameter,"%s",filename);

				get_file(data_socket,filename);
				close(data_socket);
				printf("**end get file success\n");
				exit(0);
			}else{
				int data_status;
				waitpid(data_pid, &data_status, 0);
				if(data_status==0){
					printf("ok\n");
				}else{
					printf("error\n");//TODO
				}
				status=PASS;
			}
		}
	}else if(strcmp(req.verb, "PWD")==0){
		if(status==PASS || status==PORT || status==PASV){
			send_msg(control_socket, msg_req);
			get_msg(control_socket, msg_res);
			printf("response: %s\n", msg_res);
		}
	}
	



	return 0;
}

int main(){

    if(0!=connect_to(&control_socket, "127.0.0.1", 6789)){
		return 1;
	}

	status=CONNECTED;

    char msg[SENTENCE_LEN];
    get_msg(control_socket, msg);
    printf("msg: %s\n", msg);

    while (1) {
        char sentence[SENTENCE_LEN];

        fgets(sentence, 4096, stdin);
		int len = strlen(sentence);
		sentence[len] = '\r';
		sentence[len+1] = '\n';
		sentence[len + 2] = '\0';


        printf("sentence: %s", sentence);
		

		handle_request(sentence);

		printf("status: %d\n",status);

	}

	close(control_socket);
}