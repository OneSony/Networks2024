#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <getopt.h>
#include <memory.h>
#include <netinet/in.h>
#include <poll.h>
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
#define TIMEOUT_MS_ACCEPT 60000 // 1 minute
#define TIMEOUT_MS_READ 60000   // 1 minute

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

int accept_with_timeout();

int read_with_timeout(int sockfd, char *sentence);

int basename(char *path, char *filename);

// 把越界写道convert里面吧
int path_convert(char *path); // 输入client的路径，输出server中的绝对路径

int DTP(struct request req); // 这里的路径要直接可以操作

void close_DTP(int sig);

int send_msg(int sockfd, char *sentence);

int rewrite_path(char *str);

int get_cwd(char *str);

int connect_to(int *sockfd, char *ip, int port);

int get_msg(int sockfd, char *sentence); // 会自动退出

int path_check(char *path);

int listen_at(int *sockfd, int port);

int parse_request(char *msg, struct request *req);

int handle_request(char *msg);