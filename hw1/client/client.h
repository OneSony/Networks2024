#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <getopt.h>
#include <memory.h>
#include <netinet/in.h>
#include <poll.h>
#include <regex.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define SENTENCE_LEN 8192
#define MIN_PORT 20000
#define MAX_PORT 65535
#define MAX_RES 100
#define TIMEOUT_MS_READ 60000   // 1 minute
#define TIMEOUT_MS_ACCEPT 60000 // 1 minute

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

void exit_connection();

int read_with_timeout(int sockfd, char *ch);

int accept_with_timeout(int data_listen_socket);

int basename(char *path, char *filename);

void handle_abor_main(int sig); // 主进程使用

void handle_abor_DTP(int sig); // DTP使用

void close_DTP(int sig); // 安全退出DTP

int DTP(struct request req);

int send_msg(int sockfd, char *sentence);

int connect_to(int *sockfd, char *ip, int port);

int get_msg(int sockfd,
            char *sentence); // 只会在主进程，以及没有子进程存在的时候调用

int listen_at(int *sockfd, int port);

int parse_response(char *msg, struct response *res);
int parse_request(char *msg, struct request *req);

int handle_request(char *sentence);
