#include "server.h"
// SOCKET 初始化！！！！！！！！！！
//  control socket: USER PASS QUIT SYST TYPE PORT PASV MKD CWD PWD
//  data socket: RETR STOR LIST

// TODO 获取本机ip
// TODO CWD空？

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

int basename(char *path, char *filename) {
    char *p = strrchr(path, '/');
    if (p == NULL) {
        strcpy(filename, path);
    } else {
        strcpy(filename, p + 1);
    }
    return 0;
}

int DTP(struct request req) {

    send_msg(control_socket, "150 Opening BINARY mode data connection.\r\n");

    // 建立连接
    if (status == PORT) {
        if (0 !=
            connect_to(&data_socket, port_mode_info.ip, port_mode_info.port)) {
            printf("connect_to error\n");
            send_msg(control_socket,
                     "425 no TCP connection was established\r\n");
            status = PASS;
            return 0;
        }
    } else if (status == PASV) {
        if ((data_socket = accept(data_listen_socket, NULL, NULL)) == -1) {
            printf("Error accept(): %s(%d)\n", strerror(errno), errno);
            close(data_listen_socket);
            send_msg(control_socket,
                     "425 no TCP connection was established\r\n");
            status = PASS;
            return 0;
        } else {
            close(data_listen_socket);
        }
    }

    printf("accept success\n");

    // int p_fds[2]; //父子进程通讯通道
    if (pipe(p_fds) == -1) {
        perror("pipe");
        exit(1);
    }

    printf("pipe success\n");

    int pid = fork();
    if (pid == 0) { // 创建DTP

        // 正常传输退出 0
        // 异常退出 1
        // ctrl + c退出 2
        // 用pipe传递消息
        signal(SIGTERM, close_DTP);
        close(control_socket);
        close(p_fds[0]); // 关闭管道的读取端

        int pid_signal;

        if (strcmp(req.verb, "RETR") == 0) {

            printf("send_file %s\n", req.parameter);

            file = fopen(req.parameter, "rb");
            if (file == NULL) {
                printf("Error fopen(): %s(%d)\n", strerror(errno), errno);
                pid_signal = 1;
                write(p_fds[1], &pid_signal, sizeof(pid_signal));
                close(data_socket);
                close(p_fds[1]);
                exit(1);
            }

            char buff[256];
            int n;
            while ((n = fread(buff, 1, 256, file)) > 0) { // 从file读入sockt
                if (write(data_socket, buff, n) == -1) {
                    fclose(file);
                    file = NULL;
                    perror("write");
                    printf("Error write(): %s(%d)\n", strerror(errno), errno);
                    pid_signal = 2;
                    write(p_fds[1], &pid_signal, sizeof(pid_signal));
                    close(data_socket);
                    close(p_fds[1]);
                    exit(2);
                }
            }

            printf("send file success\n");

            fclose(file);
            file = NULL;

        } else if (strcmp(req.verb, "STOR") == 0) {

            printf("get_file %s\n", req.parameter);
            char filename[256];
            basename(req.parameter, filename);
            printf("filename: %s\n", filename);
            file = fopen(filename, "wb");
            if (file == NULL) {
                printf("Error fopen(): %s(%d)\n", strerror(errno), errno);
                pid_signal = 1;
                write(p_fds[1], &pid_signal, sizeof(pid_signal));
                close(data_socket);
                close(p_fds[1]);
                exit(1);
            }

            char buff[256];
            int n;
            while ((n = read(data_socket, buff, 256)) > 0) { // 从socket读入file
                fwrite(buff, 1, n, file);                    // 本地文件！
            } // TODO 是不知道发送完的！

            fclose(file);
            file = NULL;

        } else if (strcmp(req.verb, "LIST") == 0) {
            char buffer[1024];
            char command[1025];

            if (strcmp(req.parameter, "") == 0) {
                strcpy(req.parameter, ".");
            }

            // 构建 ls 命令（指定路径）
            snprintf(command, sizeof(command), "/bin/ls -l %s",
                     req.parameter); // 第一行是总大小！！！？TODO
            printf("command: %s\n", command);

            // 打开 ls 命令的输出（只读模式）
            pfile = popen(command, "r");
            if (pfile == NULL) {
                // 如果命令执行失败
                perror("popen");
                pid_signal = 1;
                write(p_fds[1], &pid_signal, sizeof(pid_signal));
                close(data_socket);
                close(p_fds[1]);
                exit(1);
            }

            // 有问题！！！！！！！！！！！！！
            //  逐行读取 ls 的输出并通过 socket 发送给客户端
            while (fgets(buffer, sizeof(buffer), pfile) != NULL) {
                size_t len = strlen(buffer);
                if (buffer[len - 1] == '\n') {
                    buffer[len - 1] = '\r';
                    buffer[len] = '\n';
                    buffer[len + 1] = '\0';
                }

                if (send(data_socket, buffer, strlen(buffer), 0) == -1) {
                    perror("send");
                    pclose(pfile);
                    pfile = NULL;
                    perror("write");
                    printf("Error write(): %s(%d)\n", strerror(errno), errno);
                    pid_signal = 2;
                    write(p_fds[1], &pid_signal, sizeof(pid_signal));
                    close(data_socket);
                    close(p_fds[1]);
                    exit(2);
                }
                printf("%s", buffer);
            }

            // 关闭 ls 输出
            pclose(pfile);
            pfile = NULL;
        }

        close(data_socket);
        printf("**end send file success\n");

        write(p_fds[1], &pid_signal, sizeof(pid_signal)); // 向管道写入数据
        close(p_fds[1]); // 关闭管道的写入端
        exit(0);
    } else {
        close(data_socket);
        close(p_fds[1]);

        struct pollfd fds[2];

        // 设置 poll 监听 control_socket
        fds[0].fd = control_socket;
        fds[0].events = POLLIN; // 监听可读事件

        // 设置 poll 监听管道，用于接收子进程状态
        fds[1].fd = p_fds[0];
        fds[1].events = POLLIN; // 监听可读事件

        printf("Control process: Handling commands.\n");

        while (1) {
            int poll_res = poll(fds, 2, -1); // 无限等待，直到有事件发生

            if (poll_res == -1) {
                perror("poll");
                break;
            }

            // 检查控制 socket 是否有数据到达
            if (fds[0].revents & POLLIN) {

                printf("Control process: have msg.\n");
                // get msg
                char msg[SENTENCE_LEN];
                if (0 != get_msg(control_socket, msg)) { // 主进程断开 TODO
                    printf("connection error\n");
                    // TODO 清空port
                    kill(pid, SIGTERM);
                    waitpid(pid, NULL, 0); // 等待子进程终止
                    close(p_fds[0]);
                    close(control_socket);
                    printf("exit\n");
                    exit(0);
                }

                printf("msg: %s\n", msg);

                printf(":::%d %d %d %d\n", msg[0], msg[1], msg[2], msg[3]);

                struct request req;
                parse_request(msg, &req);
                printf("** verb: %s\n", req.verb);
                printf("** para: %s\n", req.parameter);

                if (strcmp(req.verb, "ABOR") == 0 ||
                    strcmp(req.verb, "QUIT") == 0) {
                    // 收到 ABOR 命令，终止文件传输
                    printf("Control process: ABOR command received.\n");
                    kill(pid, SIGTERM); // 终止子进程
                    send_msg(control_socket, "426 Transfer aborted.\r\n");
                    waitpid(pid, NULL, 0); // 等待子进程终止
                    send_msg(
                        control_socket,
                        "226 Abort command was successfully processed.\r\n");
                    break;
                }
            }

            // 检查子进程是否结束
            if (fds[1].revents & POLLIN) {
                int ret;
                read(p_fds[0], &ret, sizeof(ret));

                if (ret == 0) {
                    send_msg(control_socket, "226 Transfer complete.\r\n");
                } else if (ret == 1) {
                    send_msg(control_socket,
                             "451 Had trouble reading the file from disk.\r\n");
                } else if (ret == 2) {
                    send_msg(control_socket, "426 Transfer aborted.\r\n");

                } else {
                    send_msg(control_socket, "500 Internal error.\r\n");
                }
                break;
            }
        }

        // 清理
        close(p_fds[0]);
        waitpid(pid, NULL, 0); // 确保子进程已经终止
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
    close(p_fds[1]); // 关闭管道的写入端
    printf("Child process: Received SIGTERM, exiting...\n");
    exit(0); // 正常退出
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

            for (int i = 0; i < n; i++) {
                printf(
                    "%02x ",
                    (unsigned char)sentence[p + i]); // 打印每个字节的十六进制值
            }
            printf("\n");

            p += n;
            if (sentence[p - 2] == '\r' && sentence[p - 1] == '\n') {
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

    printf("listen success %d\n", *sockfd);

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

    sscanf(msg, "%s %[^\n]", req->verb, req->parameter);
    return 0;
}

// PASV打开data listen socket
// PORT记录
// RETR STOR LIST通过data listen socket建立data socket，内部已经关闭了data
// socket和data listen socket
int handle_request(char *msg) {
    // TODO return 0!!!
    struct request req;
    parse_request(msg, &req);
    printf("** verb: %s\n", req.verb);
    printf("** para: %s\n", req.parameter);

    if (strcmp(req.verb, "QUIT") == 0) {
        if (status == PASV) {
            close(data_listen_socket); // control socket在主函数处理
        }
        send_msg(control_socket, "221 Goodbye.\r\n");
        return 1;
    } else if (strcmp(req.verb, "ABOR") == 0) {
        if (status == PASS) {
            send_msg(control_socket, "225 No transfer to abort.\r\n");
        } else if (status == PASV) {
            close(data_socket);
            send_msg(control_socket, "225 ABOR command successful.\r\n");
        } else if (status == PORT) {
            send_msg(control_socket, "225 ABOR command successful.\r\n");
        }
        status = PASS;
    } else if (strcmp(req.verb, "USER") == 0) {
        if (status == CONNECTED) {
            if (strcmp(req.parameter, "anonymous") == 0) {
                send_msg(control_socket,
                         "331 Please provide your email address as a "
                         "password.\r\n"); // ask for an email
                status = USER;
                return 0;
            } else {
                send_msg(control_socket,
                         "530 Only \"anonymous\" can be used.\r\n");
                return 0;
            }
        }
    } else if (strcmp(req.verb, "PASS") == 0) {
        if (status == USER) {
            send_msg(control_socket, "230 Login successful.\r\n");
            status = PASS;
            return 0;
        } else {
            send_msg(control_socket,
                     "503 Please use the USER command first.\r\n");
            return 0;
        }
    } else if (strcmp(req.verb, "SYST") == 0) {
        send_msg(control_socket, "215 UNIX Type: L8\r\n");
    } else if (strcmp(req.verb, "TYPE") == 0) {
        if (strcmp(req.parameter, "I") == 0) {
            send_msg(control_socket, "200 Type set to I.\r\n");
            binary_mode = 1; // on
        } else {
            send_msg(control_socket, "500 retry just I.\r\n"); // TODO
        }
    } else if (strcmp(req.verb, "SIZE") == 0) { // TODO
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
        } else {
            send_msg(control_socket, "500 retry.\r\n");
        }
    } else if (strcmp(req.verb, "PWD") == 0) {
        if (status == PASS || status == PORT || status == PASV) {
            char path[256];

            get_cwd(path);
            printf("path: %s\n", path);

            char buff[400];
            sprintf(buff, "257 \"%s\" is the current directory.\r\n", path);
            send_msg(control_socket, buff);
            return 0;
        }
    } else if (strcmp(req.verb, "CWD") == 0) {
        if (status == PASS || status == PORT || status == PASV) {
            char path[256];
            sscanf(req.parameter, "%s", path);

            if (strcmp(path, "") == 0) {
                send_msg(control_socket, "550 Missing path.\r\n");
                return 0;
            } else {
                int ret = change_dir(path);
                if (ret == 0) {
                    send_msg(control_socket,
                             "250 Directory successfully changed.\r\n");
                    return 0;
                } else if (ret == 2) {
                    send_msg(control_socket, "550 Path is not available.\r\n");
                    return 0;
                }
            }
        }
    } else if (strcmp(req.verb, "MKD") == 0) {
        if (status == PASS || status == PORT || status == PASV) {
            char path[256];
            sscanf(req.parameter, "%s", path);

            if (mkdir(path, 0777) == 0) {
                rewrite_path(path);
                char buff[400];
                sprintf(buff, "257 \"%s\" is created.\r\n", path);
                send_msg(control_socket, buff);
            } else {
                send_msg(control_socket,
                         "550 Failed to create the directory.\r\n"); // TODO
            }
            return 0;
        }
    } else if (strcmp(req.verb, "RMD") == 0) {
        if (status == PASS || status == PORT || status == PASV) {
            char path[256];
            sscanf(req.parameter, "%s", path);

            if (rmdir(path) == 0) {
                send_msg(control_socket, "250 Directory deleted.\r\n");
            } else {
                send_msg(control_socket,
                         "550 Failed to remove the directory..\r\n"); // TODO
            }
            return 0;
        }
    } else if (strcmp(req.verb, "PORT") == 0) {
        if (status == PASS || status == PORT || status == PASV) {

            if (status == PASV) { // 关闭旧的
                close(data_listen_socket);
            }

            int p1, p2, p3, p4, p5, p6;
            sscanf(req.parameter, "%d,%d,%d,%d,%d,%d", &p1, &p2, &p3, &p4, &p5,
                   &p6);

            sprintf(port_mode_info.ip, "%d.%d.%d.%d", p1, p2, p3, p4);
            port_mode_info.port = p5 * 256 + p6;

            send_msg(control_socket, "200 PORT command successful.\r\n");
            status = PORT;

            return 0;
        }
    } else if (strcmp(req.verb, "PASV") == 0) {

        if (status == PASS || status == PASV ||
            status == PORT) { // 已开PASV重新更新

            if (status == PASV) { // 关闭旧的
                close(data_listen_socket);
            }

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

            int p1, p2, p3, p4;
            sscanf(pasv_mode_info.ip, "%d.%d.%d.%d", &p1, &p2, &p3, &p4);

            char buff[256];
            sprintf(buff, "227 Entering Passive Mode (%d,%d,%d,%d,%d,%d).\r\n",
                    p1, p2, p3, p4, pasv_mode_info.port / 256,
                    pasv_mode_info.port % 256);
            send_msg(control_socket, buff);
            status = PASV;

            return 0;
        }
    } else if (strcmp(req.verb, "RETR") == 0) {

        if (status == PORT || status == PASV) {

            int ret = file_check(req.parameter, NULL);

            if (ret == 1) {
                send_msg(control_socket,
                         "451 Paths cannot contain \"../\".\r\n");
            } else if (ret == 2) {
                send_msg(control_socket, "451 This is not a file.\r\n");
            } else if (ret == 0) {
                printf("in RETR\n");
                DTP(req);
            }
        } else {
            send_msg(control_socket,
                     "425 no TCP connection was established\r\n");
        }

        status = PASS;
        return 0;
    } else if (strcmp(req.verb, "STOR") == 0) {
        if (status == PORT || status == PASV) {

            if (path_check(req.parameter) != 0) {
                send_msg(control_socket,
                         "451 Paths cannot contain \"../\".\r\n");
            } else {
                DTP(req);
            }
        } else {
            send_msg(control_socket, "425 No TCP connection\r\n");
        }

        status = PASS;
        return 0;
    } else if (strcmp(req.verb, "LIST") == 0) {
        if (status == PORT || status == PASV) {
            DTP(req);
        } else {
            send_msg(control_socket, "425 No TCP connection\r\n");
        }

        status = PASS;
        return 0;
    } else {
        send_msg(control_socket, "502 retry\r\n");
    }

    return 0;
}

int main(int argc, char *argv[]) {
    binary_mode = 1; // on

    int port = 21;                     // 默认端口
    char root_directory[256] = "/tmp"; // 用于存储根目录，确保分配足够的空间
    strcpy(pasv_mode_info.ip, "127.0.0.1");

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-port") == 0) {
            if (i + 1 < argc) {
                port = atoi(argv[i + 1]); // 将字符串转换为整数
                i++;                      // 跳过参数
            } else {
                fprintf(stderr, "Error: -port requires an argument\n");
                exit(EXIT_FAILURE);
            }
        } else if (strcmp(argv[i], "-root") == 0) {
            if (i + 1 < argc) {
                strcpy(root_directory, argv[i + 1]); // 获取根目录字符串
                i++;                                 // 跳过参数
            } else {
                fprintf(stderr, "Error: -root requires an argument\n");
                exit(EXIT_FAILURE);
            }
        } else {
            fprintf(stderr, "Usage: %s -port <port> -root <root_directory>\n",
                    argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    printf("port: %d\n", port);
    printf("root_directory: %s\n", root_directory);

    if (change_dir(root_directory) != 0) {
        printf("Error: cannot change to root directory\n");
        exit(EXIT_FAILURE);
    }

    if (0 != listen_at(&control_listen_socket, port)) {
        return 1;
    }
    printf("listenfd: %d\n", control_listen_socket);

    while (1) {
        // 为新连接开启新的control socket
        if ((control_socket = accept(control_listen_socket, NULL, NULL)) ==
            -1) {
            printf("Error accept(): %s(%d)\n", strerror(errno), errno);
            continue;
        }

        printf("controlfd: %d\n", control_socket);

        int pid = fork();
        if (pid == 0) {
            close(control_listen_socket);

            // 子进程
            printf("controlfd: %d\n", control_socket);

            status = CONNECTED;
            send_msg(control_socket, "220 Anonymous FTP server ready.\r\n");

            while (1) {
                printf("in the loop! status:%d\n", status);
                char msg[SENTENCE_LEN];
                if (0 != get_msg(control_socket, msg)) {

                    printf("connection error\n");
                    if (status == PASV) {
                        close(data_listen_socket); // control socket在主函数处理
                    }
                    // TODO 清空port
                    break;
                }

                printf("msg: %s\n", msg);

                if (handle_request(msg) == 1) {
                    break;
                }
            }

            close(control_socket);
            exit(0);
        } else {
            close(control_socket); // 记得关！！！
        }
    }

    close(control_listen_socket);
}