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
#include <regex.h>


//LIST和消息分段有问题！！！
//连接出现问题的时候，是不是需要把链接部分放在DTP里面？？
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

int listen_at(int *sockfd, int port);            // 包装listen
int connect_to(int *sockfd, char *ip, int port); // 包装connect

int send_msg(int sockfd, char *sentence);
int get_msg(int sockfd, char *sentence);

int parse_response(char *msg, struct response *res); // 解析响应
int parse_request(char *msg, struct request *req);   // 解析请求

int get_cwd(char *str);              // 包装getcwd
int change_dir(char *path);          // 包装chdir
int send_ls(char *path, int socket); // 发送ls命令的输出

int file_check(char *filename, int *size); // 检查文件路径是否合法
int send_file(int sockfd, char *filename);
int get_file(int sockfd, char *filename);

int handle_request(char *msg);

int rewrite_path(char *str);
int path_check(char *path);

void close_DTP(int sig);

// control socket: USER PASS QUIT SYST TYPE PORT PASV MKD CWD PWD
// data socket: RETR STOR LIST

enum user_status status;

int control_listen_socket;
int control_socket;
int data_listen_socket;
int data_socket;

int binary_mode;

struct port_mode_info_s port_mode_info;
struct pasv_mode_info_s pasv_mode_info;

FILE *file = NULL;
FILE *pfile = NULL;

int p_fds[2];


int DTP(struct request req) { // TODO 错误处理
//DTP遇到问题就退出，主进程等server发消息
//主进程不需要给DTP发消息，DTP自己处理
//检查一下是不是真的会退出

    int pid = fork();
    if (pid == 0) { // 创建DTP
        signal(SIGINT, close_DTP);

        //正常传输退出 0
        //异常退出 1
        //ctrl + c退出 2
        close(control_socket);

        if (status == PORT) {

            if ((data_socket = accept(data_listen_socket, NULL, NULL)) == -1) { //TODO 超时
                printf("Error accept(): %s(%d)\n", strerror(errno), errno);
                close(data_listen_socket);
                exit(1);
            } else {
                close(data_listen_socket);
            }

        } else if (status == PASV) {
            // TODO
            if (0 !=
                connect_to(&data_socket, pasv_mode_info.ip, pasv_mode_info.port)) {
                printf("connect_to error\n");
                exit(1);
            }
        }

        //开始传输

        if (strcmp(req.verb, "RETR") == 0) {

            //printf("get_file %s\n", req.parameter);
            file = fopen(req.parameter, "wb");
            if (file == NULL) {
                printf("Error fopen(): %s(%d)\n", strerror(errno), errno);
                close(data_socket);
                exit(1);
            }

            char buff[256];
            int n;
            while ((n = read(data_socket, buff, 256)) > 0) { // 从socket读入file
                sleep(2);
                fwrite(buff, 1, n, file);
            }


            //pipe可以传输当前传递了多少

            fclose(file);
            file = NULL;

        } else if (strcmp(req.verb, "STOR") == 0) {

            file = fopen(req.parameter, "rb");
            if (file == NULL) {
                printf("Error fopen(): %s(%d)\n", strerror(errno), errno);
                close(data_socket);
                exit(1);
            }

            char buff[256];
            int n;
            while ((n = fread(buff, 1, 256, file)) > 0) { // 从file读入sockt
                write(data_socket, buff, n);
            }

            //printf("send file success\n");

            fclose(file);
            file = NULL;

        } else if (strcmp(req.verb, "LIST") == 0) {//TODO我怎么知道什么时候结束
            char buffer[1024];
            int n;

            while ((n = recv(data_socket, buffer, sizeof(buffer) - 1, 0)) > 0) {
                buffer[n] = '\0'; // 确保缓冲区是一个有效的字符串
                printf("%s", buffer);  // 打印接收到的消息
            }

            if (n == -1) {
                perror("recv");
                close(data_socket);
                exit(1);
            }

        }

        close(data_socket);
        exit(0);
    } else {
        signal(SIGINT, SIG_IGN); //TODO


        int pid_signal;
        waitpid(pid, &pid_signal, 0); // 等待子进程结束
        signal(SIGINT, SIG_DFL);


        char msg[SENTENCE_LEN];

        if(WEXITSTATUS(pid_signal)==2){//被退出  注意是WEXITSTATUS
            //TODO
            get_msg(control_socket, msg); //150
        }else{
            get_msg(control_socket, msg); //150
        }


        //键盘输入，处理ABOR
        //退出问题，等待msg
    }


    return 0;
}

void close_DTP(int sig) {
    // 处理 SIGTERM 信号，执行清理操作
    

    if (file != NULL) {
        fclose(file);
        file = NULL;
    }

    if (pfile != NULL) {
        pclose(pfile);
        pfile = NULL;
    }
    // TODO -1
    close(data_socket);
    //printf("Child process: Received SIGTERM, exiting...\n");

    exit(2); //TODO
}

int send_msg(int sockfd, char *sentence) {
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

int rewrite_path(char *str) {

    char result[1024]; // Adjust size as needed, larger than the expected string
    int j = 0;         // Index for the new buffer

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

    return 0;
}

int get_cwd(char *str) {
    if (getcwd(str, 256) == NULL) {
        printf("Error getcwd(): %s(%d)\n", strerror(errno), errno);
        return 1;
    }

    rewrite_path(str);

    return 0;
}

int change_dir(char *path) {
    if (strcmp(path, "..") == 0) {
        char str[256];
        getcwd(str, 256);
        if (strcmp(str, "/") == 0) {
            return 2;
        }
    }
    if (chdir(path) == -1) {
        printf("Error chdir(): %s(%d)\n", strerror(errno), errno);
        return 2;
    }
    return 0;
}

int send_ls(char *path, int socket) {
    FILE *ls_output;
    char buffer[1024];
    char command[256];

    // 构建 ls 命令（指定路径）
    snprintf(command, sizeof(command), "/bin/ls -l %s", path);

    // 打开 ls 命令的输出（只读模式）
    ls_output = popen(command, "r");
    if (ls_output == NULL) {
        // 如果命令执行失败
        perror("popen");
        return 2;
    }

    // 逐行读取 ls 的输出并通过 socket 发送给客户端
    while (fgets(buffer, sizeof(buffer), ls_output) != NULL) {
        size_t len = strlen(buffer);
        if (buffer[len - 1] == '\n') {
            buffer[len - 1] = '\r';
            buffer[len] = '\n';
            buffer[len + 1] = '\0';
        }
        // 发送 ls 命令的输出给客户端
        if (send(socket, buffer, strlen(buffer), 0) == -1) {
            perror("send");
            pclose(ls_output);
            return -1;
        }
        printf("%s", buffer);
    }

    // 关闭 ls 输出
    pclose(ls_output);
    return 0;
}

int send_ls_old(char *path, int socket) {
    struct dirent *entry;
    struct stat file_stat;
    DIR *dir = opendir(path);
    if (dir == NULL) {
        // 错误处理
        return 2;
    }

    // 用来存储文件信息的缓冲区
    char buffer[1024];

    // 逐个读取目录中的文件
    while ((entry = readdir(dir)) != NULL) {
        // 跳过当前目录和父目录
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        // 获取文件的状态信息
        if (stat(entry->d_name, &file_stat) == -1) {
            // 错误处理
            continue;
        }

        // 构建 EPLF 响应
        // +i=inode +s=size +m=modification_time +/ filename
        // inode, size, modification time, name are extracted
        snprintf(buffer, sizeof(buffer), "+i=%lu +s=%lld +m=%ld%s %s\r\n",
                 (unsigned long)file_stat.st_ino,       // 文件 inode
                 (long long)file_stat.st_size,          // 文件大小
                 (long)file_stat.st_mtime,              // 修改时间
                 S_ISDIR(file_stat.st_mode) ? "/" : "", // 是否为目录
                 entry->d_name);                        // 文件名
        printf("%s", buffer);
        // 发送给客户端
        send_msg(socket, buffer);
    }

    closedir(dir);
    return 0;
}

int connect_to(int *sockfd, char *ip, int port) {
    if ((*sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
        printf("Error socket(): %s(%d)\n", strerror(errno), errno);
        return 1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip);

    if (connect(*sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        printf("Error connect(): %s(%d)\n", strerror(errno), errno);
        close(*sockfd);
        return 1;
    }

    return 0;
}


int get_msg(int sockfd, char *sentence) {

    int start = 0, end=0;

    //int p = 0;


    // 定义正则表达式
    const char *last_line_pattern = "^[0-9]{3} ";
    regex_t regex;
    int reti;

    // 编译正则表达式
    reti = regcomp(&regex, last_line_pattern, REG_EXTENDED);
    if (reti) {
        fprintf(stderr, "Could not compile regex\n");
        return 0;
    }



    while(1){//读完整个回复


        while (1) {//读完句子
            char ch;
            int n = read(sockfd, &ch, 1);// 逐个字符读取
            
            if (n < 0) {
                printf("Error read(): %s(%d)\n", strerror(errno), errno);
                return -1;
            } else if (n == 0) {
                return -1; // 连接关闭
            } else {
                // 将读取到的字符存储到句子中
                sentence[end++] = ch;

                // 检查是否是\r\n
                if (end >= start+2 && sentence[end - 2] == '\r' && sentence[end - 1] == '\n') {
                    //最后一句话再结束
                    //sentence[p - 2] = '\0'; // 替换最后的\r字符为结束符
                    //p--; // 退回到结束符前一个位置
                    break;
                }

                // 确保不会超出 buffer 大小
                if (end >= SENTENCE_LEN - 1) {
                    printf("Error: buffer overflow\n");
                    return -1; // 防止溢出
                }
            }
        }

        //检查是不是最后一句

        char new_sentence[SENTENCE_LEN];
        strcpy(new_sentence,sentence+start);


        // 执行正则表达式匹配
        reti = regexec(&regex, new_sentence, 0, NULL, 0);

        if(reti==0){//匹配
            sentence[end] = '\0'; // 替换最后的\r字符为结束符???
            //TODO
            break;
        }else{
            start=end; //end是下一个要读取进来的位置
        }
    }
    printf("%s", sentence);
    regfree(&regex); // 释放正则表达式

    return 0;
}

int get_msg_old(int sockfd, char *sentence) {

    int p = 0;
    while (1) {
        int n = read(sockfd, sentence + p, SENTENCE_LEN - p);
        if (n < 0) {
            printf("Error read(): %s(%d)\n", strerror(errno), errno);
            return -1;
        } else if (n == 0) {
            return -1;
        } else {
            // p += n;
            // printf("sentence: %s\n", sentence);

            /*
            for (int i = 0; i < n; i++) {
                printf(
                    "%02x ",
                    (unsigned char)sentence[p + i]); // 打印每个字节的十六进制值
            }
            printf("\n");
            */

            p += n;
            if (sentence[p - 2] == '\r' && sentence[p - 1] == '\n') { //TODO 要分开！！！！
                sentence[p - 2] = '\0';
                break;
            }
        }
    }

    return 0;
}

int path_check(char *path) {
    if (strstr(path, "../") != NULL) {
        return 1; // 包含 '../'
    }
    return 0; // 不包含 '../'
}

int file_check(char *filename, int *size) {

    if (path_check(filename) != 0) { // 路径不合法
        return 1;
    }

    struct stat path_stat;
    // 使用 stat 函数获取文件状态
    if (stat(filename, &path_stat) != 0) {
        printf("Error stat(): %s(%d)\n", strerror(errno), errno);
        return 2;
    }

    // 检查路径是否为一个文件
    if (!S_ISREG(path_stat.st_mode)) {
        printf("Error: %s is not a file\n", filename);
        return 2; // 返回错误码，表示不是文件
    }

    if (size != NULL) {
        *size = path_stat.st_size;
    }

    return 0;
}

int send_file(int sockfd, char *filename) {
    printf("send_file %s\n", filename);

    file = fopen(filename, "rb");
    if (file == NULL) {
        printf("Error fopen(): %s(%d)\n", strerror(errno), errno);
        return 2;
    }

    // printf("send file %s\n", filename);

    char buff[256];
    int n;
    while ((n = fread(buff, 1, 256, file)) > 0) {
        sleep(0.1);
        // printf("n: %d\n", n);
        write(sockfd, buff, n);
    }

    printf("send file success\n");

    fclose(file);
    file = NULL;
    return 0;
}

int get_file(int sockfd, char *filename) {

    //printf("get_file %s\n", filename);
    file = fopen(filename, "wb");
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
    file = NULL;
    return 0;
}

int listen_at(int *sockfd, int port) {

    if ((*sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
        printf("Error socket(): %s(%d)\n", strerror(errno), errno);
        return 1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(*sockfd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
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

    //printf("listen success %d\n", *sockfd);

    return 0;
}

int parse_response(char *msg, struct response *res) {
    memset(res, 0, sizeof(*res));
    int i = 0;
    char *p = msg;
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

int parse_request(char *msg, struct request *req) {
    memset(req, 0, sizeof(*req));

    while (*msg && !isascii((unsigned char)*msg)) {
        msg++;
    }

    sscanf(msg, "%s %[^\r]\r\n", req->verb, req->parameter);
    return 0;
}

// PASV打开data listen socket
// PORT记录
// RETR STOR LIST通过data listen socket建立data socket，内部已经关闭了data
// socket和data listen socket
int handle_request(char *sentence) {
    // TODO return 0!!!
    struct request req;
    char msg[SENTENCE_LEN];
    parse_request(sentence, &req);
    printf("** verb: %s\n", req.verb);
    printf("** para: %s\n", req.parameter);
    printf("** len: %ld\n", strlen(req.parameter));

    if (strcmp(req.verb, "QUIT") == 0 || strcmp(req.verb, "ABOR") == 0) {
        send_msg(control_socket, sentence);
        get_msg(control_socket, msg);
        return 1;
    } else if (strcmp(req.verb, "USER") == 0) {
        send_msg(control_socket, sentence);
        get_msg(control_socket, msg);
    } else if (strcmp(req.verb, "PASS") == 0) {
        send_msg(control_socket, sentence);
        get_msg(control_socket, msg);
    } else if (strcmp(req.verb, "SYST") == 0) {
        send_msg(control_socket, sentence);
        get_msg(control_socket, msg);
    } else if (strcmp(req.verb, "TYPE") == 0) {
        send_msg(control_socket, sentence);
        get_msg(control_socket, msg);
    } else if (strcmp(req.verb, "PWD") == 0) {
        send_msg(control_socket, sentence);
        get_msg(control_socket, msg);
    } else if (strcmp(req.verb, "CWD") == 0) {
        //TODO 解析？？？？回复
        send_msg(control_socket, sentence);
        get_msg(control_socket, msg);
    } else if (strcmp(req.verb, "MKD") == 0) {
        send_msg(control_socket, sentence);
        get_msg(control_socket, msg);
    } else if (strcmp(req.verb, "RMD") == 0) {
        send_msg(control_socket, sentence);
        get_msg(control_socket, msg);
    } else if (strcmp(req.verb, "PORT") == 0) {
        //TODO 好像不需要记录PORT的信息

        //用户需要输入port!!!


        if(status==PORT){
            close(data_listen_socket);
        }

        if(strcmp(req.parameter, "")==0){ //未指定

            int port;

            while (1) { // 随机选一个端口
                port =
                    rand() % (MAX_PORT - MIN_PORT + 1) + MIN_PORT;
                //printf("**data_port: %d\n", port);
                if (listen_at(&data_listen_socket, port) == 0) {
                    break;
                }
            }

            //int p1, p2, p3, p4;
            //sscanf(pasv_mode_info.ip, "%d.%d.%d.%d", &p1, &p2, &p3, &p4);

            char buff[256];
            //sprintf(buff, "227 Entering Passive Mode (%d,%d,%d,%d,%d,%d).\r\n",
            //        p1, p2, p3, p4, pasv_mode_info.port / 256,
            //        pasv_mode_info.port % 256);
            sprintf(buff, "PORT 127,0,0,1,%d,%d\r\n",
                    port / 256,
                    port % 256);
            send_msg(control_socket, buff);
            get_msg(control_socket, msg);

            struct response res;
            parse_response(msg, &res);

            if(res.code[0]=='2'){
                status=PORT;
            }

        }else{//指定了
        //TODO 如果被占用了

            int ip1, ip2, ip3, ip4, port1, port2;
            sscanf(req.parameter, "%d,%d,%d,%d,%d,%d", &ip1, &ip2, &ip3, &ip4, &port1, &port2);

            if(0!=listen_at(&data_listen_socket, port1 * 256 + port2)){
                printf("Error: PORT already in use\n");
                return 0;
            }

            send_msg(control_socket, sentence);
            get_msg(control_socket, msg);

            struct response res;
            parse_response(msg, &res);

            if(res.code[0]=='2'){
                status=PORT;
            }
        }

    } else if (strcmp(req.verb, "PASV") == 0) {

        if(status==PORT){
            close(data_listen_socket);
        }

        send_msg(control_socket, sentence);
        get_msg(control_socket, msg);
 
        struct response res;
        parse_response(msg, &res);

        if(res.code[0]=='2'){
            int p1,p2,p3,p4,p5,p6;
            sscanf(res.message[0],"%*[^0-9]%d,%d,%d,%d,%d,%d%*[^\n]",&p1,&p2,&p3,&p4,&p5,&p6);
            sprintf(pasv_mode_info.ip,"%d.%d.%d.%d",p1,p2,p3,p4);
            pasv_mode_info.port=p5*256+p6;

            status=PASV;
        }
    } else if (strcmp(req.verb, "RETR") == 0 || strcmp(req.verb, "STOR") == 0 || strcmp(req.verb, "LIST") == 0) {
        //TODO
        //到当前目录
        if(status==PORT || status==PASV){
            send_msg(control_socket, sentence);
            char msg[SENTENCE_LEN];
            get_msg(control_socket, msg);
            struct response res;
            parse_response(msg, &res);
            if(res.code[0]=='1'){//可以连接
                DTP(req);
            }else{
                printf("Error: %s\n", msg);
            }
        }else{
            printf("Error: PORT or PASV not established\n");
        }

        status=PASS;
    } else {
        //printf("Error: Unsupport command\n");
    }

    return 0;
}

int main(int argc, char *argv[]) {

    //signal(SIGINT, SIG_IGN);

    
	char server_ip[16] = "127.0.0.1";
	int server_port = 21;


    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-ip") == 0) {
            if (i + 1 < argc) {
                strcpy(server_ip, argv[i + 1]); // 获取根目录字符串
                i++;                                 // 跳过参数
            } else {
                fprintf(stderr, "Error: -root requires an argument\n");
                exit(EXIT_FAILURE);
            }
        } else if (strcmp(argv[i], "-port") == 0) {
            if (i + 1 < argc) {
                server_port = atoi(argv[i + 1]); // 将字符串转换为整数
                i++;                      // 跳过参数
            } else {
                fprintf(stderr, "Error: -port requires an argument\n");
                exit(EXIT_FAILURE);
            }
        } else {
            fprintf(stderr, "Usage: %s -port <port> -root <root_directory>\n",
                    argv[0]);
            exit(EXIT_FAILURE);
        }
    }

 	//printf("ip: %s\n", server_ip);
    //printf("port: %d\n", server_port);


    if(0!=connect_to(&control_socket, server_ip, server_port)){
		return 1;
	}

    status=CONNECTED;

    //欢迎信息
    char msg[SENTENCE_LEN];
    get_msg(control_socket, msg);


    while (1) {


        //用户输入
        char sentence[SENTENCE_LEN];

        printf("myftp> ");
        //fflush(stdin);
        fgets(sentence, 4096, stdin);
		int len = strlen(sentence);
		sentence[len-1] = '\r';
		sentence[len] = '\n';
		sentence[len + 1] = '\0';


        //进入循环，同时接受responce，直接返回结果是否正确
        if(1==handle_request(sentence)){
            break;
        }

		printf("status: %d\n",status);


        //TODO status!!!
        //现在直接进PASV会卡死，不PASS
		//printf("status: %d\n",status);


	}

	close(control_socket);
}