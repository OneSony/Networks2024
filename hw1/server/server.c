#include <netinet/in.h>
#include <sys/socket.h>

#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <stdlib.h>

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

// control socket: USER PASS QUIT SYST TYPE PORT PASV MKD CWD PWD
// data socket: RETR STOR LIST

enum user_status { CONNECTED, USER, PASS, PORT, PASV, RERT, STOR };

enum user_status status;

struct request {
    char verb[5];
    char parameter[256];
};

struct response {
    char code[4];
    char message[MAX_RES][256];
};

int control_listen_socket;
int control_socket;
int data_listen_socket;
int data_socket;

int data_pid;

struct port_mode_info_s {
    char ip[16];
    int port;
};

struct pasv_mode_info_s {
    int port;
};

struct port_mode_info_s port_mode_info;
struct pasv_mode_info_s pasv_mode_info;

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

int get_cwd(char *str) {
    if (getcwd(str, 256) == NULL) {
        printf("Error getcwd(): %s(%d)\n", strerror(errno), errno);
        return 1;
    }

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

int get_msg(int sockfd, char *sentence) {

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

int file_check(char *filename, int *size) {
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

    FILE *file = fopen(filename, "rb");
    if (file == NULL) {
        printf("Error fopen(): %s(%d)\n", strerror(errno), errno);
        return 2;
    }

    printf("send file %s\n", filename);

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

int get_file(int sockfd, char *filename) {

    printf("get_file %s\n", filename);
    FILE *file = fopen(filename, "wb");
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

int listen_at(int *sockfd, int port) {
    printf("*****listen_at %d\n", port);
    printf("*****listen_at %d\n", *sockfd);

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

    printf("listen success %d\n", *sockfd);

    return 0;
}

int parse_response(char *msg, struct response *res) {
    memset(res, 0, sizeof(res));
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
    memset(req, 0, sizeof(req));
    sscanf(msg, "%s %[^\n]", req->verb, req->parameter);
    return 0;
}

int handle_request(char *msg) {

    struct request req;
    parse_request(msg, &req);
    printf("req.verb: %s\n", req.verb);
    printf("req.parameter: %s\n", req.parameter);

    if (strcmp(req.verb, "QUIT") == 0 || strcmp(req.verb, "ABOR") == 0) {
        send_msg(control_socket, "221 Goodbye.\r\n");
        return 1;
    } else if (strcmp(req.verb, "SYST") == 0) {
        send_msg(control_socket, "215 UNIX Type: L8\r\n");
    } else if (strcmp(req.verb, "TYPE") == 0) {
        if (strcmp(req.parameter, "I") == 0) {
            send_msg(control_socket, "200 Type set to I.\r\n");
        } else {
            send_msg(control_socket, "500 retry just I.\r\n");
        }
    } else if (strcmp(req.verb, "SIZE") == 0) {
        int size;
        int ret = file_check(req.parameter, &size);

        if (ret == 0) {
            char buff[256];
            sprintf(buff, "213 %d\r\n", size);
            send_msg(control_socket, buff);
            printf("size: %d\n", size);
        } else if (ret == 1) {
            send_msg(control_socket, "500 retry.\r\n");
            printf("1\n");
        } else if (ret == 2) {
            send_msg(control_socket, "451 not a file.\r\n");
            printf("2\n");
        }else{
			send_msg(control_socket, "500 retry.\r\n");
		}
    } else if (strcmp(req.verb, "USER") == 0) {
        if (status == CONNECTED) {
            if (strcmp(req.parameter, "anonymous") == 0) {
                send_msg(
                    control_socket,
                    "331 Please specify the password.\r\n"); // ask for an email
                status = USER;
            } else {
                send_msg(control_socket, "500 retry username.\r\n");
            }
        } else {
            send_msg(control_socket, "500 retry.\r\n");
        }
    } else if (strcmp(req.verb, "PASS") == 0) {
        if (status == USER) {
            send_msg(control_socket, "230 Login successful.\r\n");
            status = PASS;
        } else {
            send_msg(control_socket, "500 please use username.\r\n");
        }
    } else if (strcmp(req.verb, "PORT") == 0) {
        if (status == PASS) {
            int p1, p2, p3, p4, p5, p6;
            sscanf(req.parameter, "%d,%d,%d,%d,%d,%d", &p1, &p2, &p3, &p4, &p5,
                   &p6);

            sprintf(port_mode_info.ip, "%d.%d.%d.%d", p1, p2, p3, p4);
            port_mode_info.port = p5 * 256 + p6;

            send_msg(control_socket, "200 PORT command successful.\r\n");
            status = PORT;
        } else {
            send_msg(control_socket, "500 not login.\r\n");
        }
    } else if (strcmp(req.verb, "PASV") == 0) {
        if (status == PASS) {
            // TODO pasv ip
            printf("data_port: %d\n", data_listen_socket);
            while (1) { // 随机选一个端口
                pasv_mode_info.port =
                    rand() % (MAX_PORT - MIN_PORT + 1) + MIN_PORT;
                printf("**data_port: %d\n", pasv_mode_info.port);
                if (listen_at(&data_listen_socket, pasv_mode_info.port) == 0) {
                    break;
                }
            }
            printf("data_port: %d\n", data_listen_socket);

            char buff[256];
            sprintf(buff, "227 Entering Passive Mode (127,0,0,1,%d,%d).\r\n",
                    pasv_mode_info.port / 256, pasv_mode_info.port % 256);
            send_msg(control_socket, buff);
            status = PASV;
        } else {
            send_msg(control_socket, "500 not login.\r\n");
        }
    } else if (strcmp(req.verb, "RETR") == 0) {
        if (status == PORT || status == PASV) {

            if (file_check(req.parameter, NULL) != 0) {
                send_msg(control_socket, "451 not a file.\r\n");
            } else {

                // 建立连接
                if (status == PORT) {
                    // TODO
                    if (0 != connect_to(&data_socket, port_mode_info.ip,
                                        port_mode_info.port)) {
                        printf("connect_to error\n");
                        exit(1);
                    }
                } else if (status == PASV) {
                    if ((data_socket =
                             accept(data_listen_socket, NULL, NULL)) == -1) {
                        printf("Error accept(): %s(%d)\n", strerror(errno),
                               errno);
                        close(data_listen_socket);
                        exit(1);
                    }
                }

                printf("accept success\n");
                send_msg(control_socket,
                         "150 Opening BINARY mode data connection.\r\n");

                data_pid = fork();
                if (data_pid == 0) { // 创建DTP
                    close(control_socket);

                    // send
                    int ret = send_file(data_socket, req.parameter);
                    close(data_socket);
                    printf("**end send file success\n");
                    exit(ret);
                } else {
                    close(data_socket);

                    printf("pid: %d\n", data_pid);

                    int data_status;
                    waitpid(data_pid, &data_status, 0);

                    if (data_status == 0) {
                        send_msg(control_socket, "226 Transfer complete.\r\n");
                    } else if (data_status == 1) {
                        send_msg(control_socket,
                                 "425 no TCP connection was established\r\n");
                    } else if (data_status == 2) { // cannot open file
                        send_msg(
                            control_socket,
                            "451 had trouble reading the file from disk.\r\n");
                    } else {
                        send_msg(control_socket, "500 Internal error.\r\n");
                    }
                }
            }
        } else {
            send_msg(control_socket, "425 No TCP connection\r\n");
        }

        status = PASS;
    } else if (strcmp(req.verb, "STOR") == 0) {
        if (status == PORT || status == PASV) {

            // 建立连接
            if (status == PORT) {
                // TODO
                if (0 != connect_to(&data_socket, port_mode_info.ip,
                                    port_mode_info.port)) {
                    printf("connect_to error\n");
                    exit(1);
                }
            } else if (status == PASV) {
                if ((data_socket =
                            accept(data_listen_socket, NULL, NULL)) == -1) {
                    printf("Error accept(): %s(%d)\n", strerror(errno),
                            errno);
                    close(data_listen_socket);
                    exit(1);
                }
            }

            printf("accept success\n");
            send_msg(control_socket,
                        "150 Opening BINARY mode data connection.\r\n");

            data_pid = fork();
            if (data_pid == 0) { // 创建DTP
                close(control_socket);

                // send
                int ret = get_file(data_socket, req.parameter);
                close(data_socket);
                printf("**end send file success\n");
                exit(ret);
            } else {
                close(data_socket);

                printf("pid: %d\n", data_pid);

                int data_status;
                waitpid(data_pid, &data_status, 0);

                if (data_status == 0) {
                    send_msg(control_socket, "226 Transfer complete.\r\n");
                } else if (data_status == 1) {
                    send_msg(control_socket,
                                "425 no TCP connection was established\r\n");
                } else if (data_status == 2) { // cannot open file
                    send_msg(
                        control_socket,
                        "451 had trouble reading the file from disk.\r\n");
                } else {
                    send_msg(control_socket, "500 Internal error.\r\n");
                }
            }
        } else {
            send_msg(control_socket, "425 No TCP connection\r\n");
        }

        status = PASS;
    } else if (strcmp(req.verb, "LIST") == 0) {
        if (status == PORT || status == PASV) {

            // 建立连接
            if (status == PORT) {
                // TODO
                if (0 != connect_to(&data_socket, port_mode_info.ip,
                                    port_mode_info.port)) {
                    printf("connect_to error\n");
                    exit(1);
                }
            } else if (status == PASV) {
                if ((data_socket = accept(data_listen_socket, NULL, NULL)) ==
                    -1) {
                    printf("Error accept(): %s(%d)\n", strerror(errno), errno);
                    close(data_listen_socket);
                    exit(1);
                }
            }

            printf("accept success\n");
            send_msg(control_socket,
                     "150 Opening BINARY mode data connection.\r\n");

            data_pid = fork();
            if (data_pid == 0) { // 创建DTP
                close(control_socket);

                if (strcmp(req.parameter, "") == 0) {
                    strcpy(req.parameter, ".");
                }
                int ret = send_ls(req.parameter, data_socket);

                close(data_socket);
                printf("**end send file success\n");
                exit(ret);
            } else {
                close(data_socket);

                printf("pid: %d\n", data_pid);

                int data_status;
                waitpid(data_pid, &data_status, 0);

                if (data_status == 0) {
                    send_msg(control_socket, "226 Transfer complete.\r\n");
                } else if (data_status == 1) {
                    send_msg(control_socket,
                             "425 no TCP connection was established\r\n");
                } else if (data_status == 2) { // cannot open file
                    send_msg(control_socket,
                             "451 had trouble reading the file from disk.\r\n");
                } else {
                    send_msg(control_socket, "500 Internal error.\r\n");
                }
            }
        } else {
            send_msg(control_socket, "425 No TCP connection\r\n");
        }

        status = PASS;
    } else if (strcmp(req.verb, "PWD") == 0) {
        if (status == PASS || status == PORT || status == PASV) {
            char path[256];

            get_cwd(path);
            printf("path: %s\n", path);

            char buff[256];
            sprintf(buff, "257 \"%s\" is the current directory.\r\n", path);
            send_msg(control_socket, buff);
            return 0;
        } else {
            send_msg(control_socket, "500 please login.\r\n");
        }
    } else if (strcmp(req.verb, "CWD") == 0) {
        if (status == PASS || status == PORT || status == PASV) {
            char path[256];
            sscanf(req.parameter, "%s", path);

            int ret = change_dir(path);
            if (ret == 0) {
                send_msg(control_socket,
                         "250 Directory successfully changed.\r\n");
            } else if (ret == 2) {
                send_msg(control_socket, "550 unknown.\r\n"); // TODO
            } else {
                send_msg(control_socket, "500 retry.\r\n");
            }
        }
    } else if (strcmp(req.verb, "MKD") == 0) {
        if (status == PASS || status == PORT || status == PASV) {
            char path[256];
            sscanf(req.parameter, "%s", path);

            if (mkdir(path, 0777) == 0) {
                send_msg(control_socket, "257 Directory created.\r\n");
            } else {
                send_msg(control_socket, "500 retry.\r\n");
            }
        }
    } else if (strcmp(req.verb, "RMD") == 0) {
        if (status == PASS || status == PORT || status == PASV) {
            char path[256];
            sscanf(req.parameter, "%s", path);

            if (rmdir(path) == 0) {
                send_msg(control_socket, "250 Directory deleted.\r\n");
            } else {
                send_msg(control_socket, "500 retry.\r\n");
            }
        }
    } else {
        send_msg(control_socket, "502 retry\r\n");
    }

    // return 2;
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

int main(int argc, char *argv[]) {

	int opt;
    int port = 21; // 默认端口
    char root_directory[256] = "/tmp"; // 用于存储根目录，确保分配足够的空间
    

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-port") == 0) {
            if (i + 1 < argc) {
                port = atoi(argv[i + 1]); // 将字符串转换为整数
                i++; // 跳过参数
            } else {
                fprintf(stderr, "Error: -port requires an argument\n");
                exit(EXIT_FAILURE);
            }
        } else if (strcmp(argv[i], "-root") == 0) {
            if (i + 1 < argc) {
                strcpy(root_directory, argv[i + 1]); // 获取根目录字符串
                i++; // 跳过参数
            } else {
                fprintf(stderr, "Error: -root requires an argument\n");
                exit(EXIT_FAILURE);
            }
        } else {
            fprintf(stderr, "Usage: %s -port <port> -root <root_directory>\n", argv[0]);
            exit(EXIT_FAILURE);
        }
    }

	printf("port: %d\n", port);
	printf("root_directory: %s\n", root_directory);

	if(change_dir(root_directory) != 0){
		printf("Error: cannot change to root directory\n");
		exit(EXIT_FAILURE);
	}


    if (0 != listen_at(&control_listen_socket, port)) {
        return 1;
    }
    printf("listenfd: %d\n", control_listen_socket);

    while (1) {

        if ((control_socket = accept(control_listen_socket, NULL, NULL)) ==
            -1) {
            printf("Error accept(): %s(%d)\n", strerror(errno), errno);
            continue;
        }

        printf("controlfd: %d\n", control_socket);

        int pid = fork();
        if (pid == 0) {
            // 子进程
            printf("controlfd: %d\n", control_socket);

            status = CONNECTED;
            close(control_listen_socket);
            send_msg(control_socket, "220 Anonymous FTP server ready.\r\n");

            while (1) {
                printf("in the loop! status:%d\n", status);
                char msg[SENTENCE_LEN];
                if (0 != get_msg(control_socket, msg)) {
                    printf("get_msg error\n");
                    // TODO 清空port
                    break;
                }

                printf("msg: %s\n", msg);

                if (handle_request(msg) == 1) {
                    break;
                }

                // TODO 连接断开
            }

            close(control_socket);
            return 0;
        } else {
            close(control_socket); // 记得关！！！
        }
    }

    close(control_listen_socket);
}