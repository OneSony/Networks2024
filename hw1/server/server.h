#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <memory.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>

#define SENTENCE_LEN 8192
#define MIN_PORT 20000
#define MAX_PORT 65535
#define MAX_RES 100

enum user_status { CONNECTED, USER, PASS, PORT, PASV, RERT, STOR };

struct request {
    char verb[5];
    char parameter[256];
};

struct response {
    char code[4];
    char message[MAX_RES][256];
};


struct port_mode_info_s {
    char ip[16];
    int port;
};

struct pasv_mode_info_s {
    char ip[16];
    int port;
};


int listen_at(int *sockfd, int port); //包装listen
int connect_to(int *sockfd, char *ip, int port);//包装connect

int send_msg(int sockfd, char *sentence);
int get_msg(int sockfd, char *sentence);

int parse_response(char *msg, struct response *res);//解析响应
int parse_request(char *msg, struct request *req);//解析请求

int get_cwd(char *str); //包装getcwd
int change_dir(char *path); //包装chdir
int send_ls(char *path, int socket); //发送ls命令的输出

int file_check(char *filename, int *size); //检查文件路径是否合法
int send_file(int sockfd, char *filename);
int get_file(int sockfd, char *filename);

int handle_request(char *msg);

int rewrite_path(char* str);
int path_check(char *path);

