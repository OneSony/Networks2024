#include "server.h"

enum user_status status;

int control_listen_socket = -1;
int control_socket = -1;
int data_listen_socket = -1;
int data_socket = -1;

int binary_mode;

long long offset = 0;

int print_pid; // 区分输出的pid

struct port_mode_info_s port_mode_info;
struct pasv_mode_info_s pasv_mode_info;

FILE *file = NULL;
FILE *pfile = NULL;

char root_directory[256];

int p_fds[2] = {-1, -1};

int dtp_pid = -1;

void exit_connection() {

    if (dtp_pid != -1 && kill(dtp_pid, 0) == 0) {
        kill(dtp_pid, SIGTERM);
        waitpid(dtp_pid, NULL, 0); // 等待子进程终止
    }

    if (control_socket != -1) {
        close(control_socket);
    }

    if (control_listen_socket != -1) {
        close(control_listen_socket);
    }

    if (data_listen_socket != -1) {
        close(data_listen_socket);
    }

    if (data_socket != -1) {
        close(data_socket);
    }

    if (p_fds[0] != -1) {
        close(p_fds[0]);
    }

    if (p_fds[1] != -1) {
        close(p_fds[1]);
    }

    if (file != NULL) {
        fclose(file);
        file = NULL;
    }

    if (pfile != NULL) {
        pclose(pfile);
        pfile = NULL;
    }

    exit(0);
}

int accept_with_timeout(int data_listen_socket) {

    struct pollfd fds[1];
    int ret, data_socket;

    fds[0].fd = data_listen_socket;
    fds[0].events = POLLIN; // We are waiting for data to be ready to read
                            // (incoming connection)

    // Wait for data to be ready or timeout
    ret = poll(fds, 1, TIMEOUT_MS_ACCEPT);

    if (ret == -1) {
        // Error during poll
        printf("\033[31m[%d Error]\033[0m poll(): %s(%d)\n", print_pid,
               strerror(errno), errno);
        return -1;
    } else if (ret == 0) {
        // Timeout occurred
        printf("\033[34m[%d Info]\033[0m Timeout after %d minute waiting for a "
               "connection.\n",
               print_pid, TIMEOUT_MS_ACCEPT / 60000);
        return -1;
    } else {
        // There is a connection to accept
        if (fds[0].revents & POLLIN) {
            data_socket = accept(data_listen_socket, NULL, NULL);
            if (data_socket == -1) {
                printf("\033[31m[%d Error]\033[0m accept(): %s(%d)\n",
                       print_pid, strerror(errno), errno);
                return -1;
            }
        }
    }

    return data_socket;
}

int read_with_timeout(int sockfd, char *sentence) {

    struct pollfd fds[1];
    int ret;

    // Set up the poll structure
    fds[0].fd = sockfd;
    fds[0].events = POLLIN; // We are waiting for input (readable data)

    // Poll with a timeout of 1 minute
    ret = poll(fds, 1, TIMEOUT_MS_READ);

    if (ret == -1) {
        printf("\033[31m[%d Error]\033[0m poll(): %s(%d)\n", print_pid,
               strerror(errno), errno);
        return -1;
    } else if (ret == 0) {
        // Timeout occurred
        printf("\033[34m[%d Info]\033[0m Timeout after %d minute waiting for a "
               "request, about to close\n",
               print_pid, TIMEOUT_MS_READ / 60000);
        return 0; // Timeout, exit the function， 类似关闭了
    } else {

        // There is data to read

        int p = 0;
        while (1) {
            // printf("in\n");
            int n = read(sockfd, sentence + p, SENTENCE_LEN - p);
            if (n < 0) {
                printf("\033[31m[%d Error]\033[0m read(): %s(%d)\n", print_pid,
                       strerror(errno), errno);
                return -1;
            } else if (n == 0) { // close
                printf("\033[31m[%d Error]\033[0m read(): %s(%d)\n", print_pid,
                       strerror(errno), errno);
                printf("\033[31m[%d Error]\033[0m Connection lost.\n",
                       print_pid);
                exit_connection();
                return 0; // 关闭
            } else {
                p += n;
                // printf("p: %d\n", p);
                if (sentence[p - 2] == '\r' && sentence[p - 1] == '\n') {
                    sentence[p - 2] = '\0';
                    // printf("sentence: %s\n", sentence);
                    break;
                }
            }
        }
    }

    return 1; // Continue reading
}

int basename(char *path, char *filename) {
    char *p = strrchr(path, '/');
    if (p == NULL) {
        strcpy(filename, path);
    } else {
        strcpy(filename, p + 1);
    }
    return 0;
}

// 把越界写道convert里面吧
int path_convert(char *path) { // 输入client的路径，输出server中的绝对路径
    char server_path[600];
    if (path[0] == '/') { // Absolute path
        strcpy(server_path, root_directory);
        strcat(server_path, path);
    } else {
        strcpy(server_path, path);
    }

    // printf("server_path: %s\n", server_path);

    char resolved_path[800];

    if (realpath(server_path, resolved_path) == NULL) { // 不存在
        printf("\033[31m[%d Error]\033[0m realpath error for root: %s (%d)\n",
               print_pid, strerror(errno), errno);
        return 1;
    }

    // printf("resolved_path: %s\n", resolved_path);

    if (resolved_path[strlen(resolved_path) - 1] ==
        '/') { // root_directory最后不含"/"
        resolved_path[strlen(resolved_path) - 1] = '\0';
    }

    if (strncmp(resolved_path, root_directory, strlen(root_directory)) !=
        0) { // 越界了
        return 2;
    }

    strcpy(path, resolved_path);

    return 0;
}

int DTP(struct request req) { // 这里的路径要直接可以操作

    send_msg(control_socket, "150 Opening BINARY mode data connection.\r\n");

    // 建立连接
    if (status == PORT) {
        if (0 !=
            connect_to(&data_socket, port_mode_info.ip, port_mode_info.port)) {
            data_socket = -1;
            // printf("connect_to error\n");
            send_msg(control_socket,
                     "425 no TCP connection was established\r\n");
            status = PASS;
            return 0;
        }
    } else if (status == PASV) {

        if ((data_socket = accept_with_timeout(data_listen_socket)) == -1) {
            close(data_listen_socket);
            data_listen_socket = -1;
            send_msg(control_socket,
                     "425 no TCP connection was established\r\n");
            status = PASS;
            return 0;
        } else {
            close(data_listen_socket);
            data_listen_socket = -1;
        }
    }

    printf("\033[34m[%d Info]\033[0m DTP is created.\n", print_pid);

    // int p_fds[2]; //父子进程通讯通道
    if (pipe(p_fds) == -1) {
        printf("\033[31m[%d Error]\033[0m pipe: %s (%d)\n", print_pid,
               strerror(errno), errno);
        send_msg(control_socket, "500 Internal error.\r\n"); // 还在主进程里

        close(data_socket);
        data_socket = -1;
        status = PASS;
        return 0;
    }

    // printf("pipe success\n");

    dtp_pid = fork();
    if (dtp_pid == 0) { // 创建DTP

        // 正常传输退出 0
        // 异常退出 1
        // ctrl + c退出 2
        // 用pipe传递消息
        signal(SIGTERM, close_DTP);
        close(control_socket);
        control_socket = -1;
        close(p_fds[0]); // 关闭管道的读取端
        p_fds[0] = -1;

        int pid_signal = 0;

        if (strcmp(req.verb, "RETR") == 0) {

            printf("\033[34m[%d Info]\033[0m Sending: %s\n", print_pid,
                   req.parameter);

            file = fopen(req.parameter, "rb");
            if (file == NULL) {
                printf("\033[31m[%d Error]\033[0m fopen(): %s(%d)\n", print_pid,
                       strerror(errno), errno);
                pid_signal = 1;
                write(p_fds[1], &pid_signal, sizeof(pid_signal));
                close(data_socket);
                data_socket = -1;
                close(p_fds[1]);
                p_fds[1] = -1;
                exit(1);
            }

            if (offset != 0) {
                if (fseek(file, offset, SEEK_SET) != 0) {
                    printf("\033[31m[%d Error]\033[0m fseek: %s (%d)\n",
                           print_pid, strerror(errno), errno);
                    fclose(file);
                    file = NULL;
                    pid_signal = 1;
                    write(p_fds[1], &pid_signal, sizeof(pid_signal));
                    close(data_socket);
                    data_socket = -1;
                    close(p_fds[1]);
                    p_fds[1] = -1;
                    exit(1); // TODO 有必要退出的，因为此时client以为从这里开始
                }
            }

            // printf("RETR offset: %lld\n", offset);

            char buff[256];
            int n;
            while ((n = fread(buff, 1, 256, file)) > 0) { // 从file读入sockt
                // printf("n: %d\n", n);
                if (write(data_socket, buff, n) == -1) {
                    fclose(file);
                    file = NULL;
                    printf("\033[31m[%d Error]\033[0m write(): %s(%d)\n",
                           print_pid, strerror(errno), errno);
                    pid_signal = 2;
                    write(p_fds[1], &pid_signal, sizeof(pid_signal));
                    close(data_socket);
                    data_socket = -1;
                    close(p_fds[1]);
                    p_fds[1] = -1;
                    exit(2);
                }
            }

            // printf("\033[34m[%d Info]\033[0m File transfer completed.\n",
            // print_pid);

            fclose(file);
            file = NULL;

        } else if (strcmp(req.verb, "STOR") == 0) {

            printf("\033[34m[%d Info]\033[0m Getting: %s\n", print_pid,
                   req.parameter);

            if (offset != 0) {
                file = fopen(req.parameter, "ab");
            } else {
                file = fopen(req.parameter, "wb");
            }

            if (file == NULL) {
                printf("\033[31m[%d Error]\033[0m fopen(): %s(%d)\n", print_pid,
                       strerror(errno), errno);
                pid_signal = 1;
                write(p_fds[1], &pid_signal, sizeof(pid_signal));
                close(data_socket);
                data_socket = -1;
                close(p_fds[1]);
                p_fds[1] = -1;
                exit(1);
            }

            if (offset != 0) {
                if (fseek(file, offset, SEEK_SET) != 0) {
                    printf("\033[31m[%d Error]\033[0m fseek: %s (%d)\n",
                           print_pid, strerror(errno), errno);
                    fclose(file);
                    file = NULL;
                    pid_signal = 1;
                    write(p_fds[1], &pid_signal, sizeof(pid_signal));
                    close(data_socket);
                    data_socket = -1;
                    close(p_fds[1]);
                    p_fds[1] = -1;
                    exit(1); // TODO
                }
            }

            char buff[256];
            int n;
            while ((n = read(data_socket, buff, 256)) > 0) { // 从socket读入file
                fwrite(buff, 1, n, file);                    // 本地文件！
            } // TODO 是不知道发送完的！

            if (n == -1) {
                fclose(file);
                file = NULL;
                printf("\033[31m[%d Error]\033[0m read(): %s(%d)\n", print_pid,
                       strerror(errno), errno);
                pid_signal = 2;
                write(p_fds[1], &pid_signal, sizeof(pid_signal));
                close(data_socket);
                data_socket = -1;
                close(p_fds[1]);
                p_fds[1] = -1;
                exit(2);
            }

            fclose(file);
            file = NULL;

        } else if (strcmp(req.verb, "LIST") == 0 ||
                   strcmp(req.verb, "NLST") == 0) {
            char buffer[1024];
            char command[1025];

            // 构建 ls 命令（指定路径）

            if (strcmp(req.verb, "LIST") == 0) {
                snprintf(command, sizeof(command), "/bin/ls -l %s",
                         req.parameter); // TODO 第一行是总大小！！！
            } else if (strcmp(req.verb, "NLST") == 0) {
                // snprintf(command, sizeof(command), "find '%s' -maxdepth 1
                // -type d -not -name '.' -printf '%%f/\\n' -o -type f -printf
                // '%%f\\n'", req.parameter); snprintf(command, sizeof(command),
                // "ls -1F %s", req.parameter);
                snprintf(command, sizeof(command),
                         "ls -1F %s | sed 's/\\*\\|@\\|=\\|\\s*$//g'",
                         req.parameter);
            }
            // printf("command: %s\n", command);

            // 打开 ls 命令的输出（只读模式）
            pfile = popen(command, "r");
            if (pfile == NULL) {
                // 如果命令执行失败
                printf("\033[31m[%d Error]\033[0m popen(): %s(%d)\n", print_pid,
                       strerror(errno), errno);
                pid_signal = 1;
                write(p_fds[1], &pid_signal, sizeof(pid_signal));
                close(data_socket);
                data_socket = -1;
                close(p_fds[1]);
                p_fds[1] = -1;
                exit(1);
            }

            // 有问题！！！！！！！！！！！！！
            //  逐行读取 ls 的输出并通过 socket 发送给客户端
            while (fgets(buffer, sizeof(buffer), pfile) != NULL) {

                if (strcmp(req.verb, "NLST") == 0) { // 替换空字符
                    for (size_t i = 0; i < strlen(buffer); i++) {
                        if (buffer[i] == '\000') {
                            buffer[i] = '\012'; // 替换为换行符
                        }
                    }
                }

                size_t len = strlen(buffer);
                if (buffer[len - 1] == '\n') {
                    buffer[len - 1] = '\r';
                    buffer[len] = '\n';
                    buffer[len + 1] = '\0';
                }

                if (send(data_socket, buffer, strlen(buffer), 0) == -1) {
                    printf("\033[31m[%d Error]\033[0m send(): %s(%d)\n",
                           print_pid, strerror(errno), errno);
                    pclose(pfile);
                    pfile = NULL;
                    pid_signal = 2;
                    write(p_fds[1], &pid_signal, sizeof(pid_signal));
                    close(data_socket);
                    data_socket = -1;
                    close(p_fds[1]);
                    p_fds[1] = -1;
                    exit(2);
                }
                // printf("%s", buffer);
            }

            // 关闭 ls 输出
            pclose(pfile);
            pfile = NULL;
        }

        close(data_socket);
        data_socket = -1;
        printf("\033[34m[%d Info]\033[0m Transfer completed.\n", print_pid);

        write(p_fds[1], &pid_signal, sizeof(pid_signal)); // 向管道写入数据
        close(p_fds[1]); // 关闭管道的写入端
        p_fds[1] = -1;
        exit(0);
    } else if (dtp_pid > 0) {
        close(data_socket);
        data_socket = -1;
        close(p_fds[1]);
        p_fds[1] = -1;

        struct pollfd fds[2];

        // 设置 poll 监听 control_socket
        fds[0].fd = control_socket;
        fds[0].events = POLLIN; // 监听可读事件

        // 设置 poll 监听管道，用于接收子进程状态
        fds[1].fd = p_fds[0];
        fds[1].events = POLLIN; // 监听可读事件

        // printf("Control process: Handling commands.\n");

        while (1) {
            int poll_res = poll(fds, 2, -1); // 无限等待，直到有事件发生

            if (poll_res == -1) {
                printf("\033[31m[%d Error]\033[0m poll(): %s(%d)\n", print_pid,
                       strerror(errno), errno);
                send_msg(control_socket, "500 Internal error.\r\n");
                kill(dtp_pid, SIGTERM);
                break;
            }

            // 检查控制 socket 是否有数据到达
            if (fds[0].revents & POLLIN) {

                // printf("Control process: have msg.\n");
                //  get msg
                char msg[SENTENCE_LEN];
                get_msg(control_socket, msg); // 这里有限时也没关系, 因为有poll

                // printf("msg: %s\n", msg);

                // printf(":::%d %d %d %d\n", msg[0], msg[1], msg[2], msg[3]);

                struct request req;
                parse_request(msg, &req);
                printf("\033[33m[%d GET]\033[0m %s; %s\n", print_pid, req.verb,
                       req.parameter);

                if (strcmp(req.verb, "ABOR") == 0 ||
                    strcmp(req.verb, "QUIT") == 0) {
                    // 收到 ABOR 命令，终止文件传输
                    // printf("Control process: ABOR command received.\n");
                    kill(dtp_pid, SIGTERM); // 终止子进程
                    send_msg(control_socket, "426 Transfer aborted.\r\n");
                    waitpid(dtp_pid, NULL, 0); // 等待子进程终止
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

                // printf("code %d.\n", ret);

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
        p_fds[0] = -1;
        waitpid(dtp_pid, NULL, 0); // 确保子进程已经终止
    } else {                       // 错误
        printf("\033[31m[%d Error]\033[0m fork(): %s(%d)\n", print_pid,
               strerror(errno), errno);
        send_msg(control_socket, "500 Internal error.\r\n");
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

    if (data_listen_socket != -1) {
        close(data_listen_socket);
    }

    if (data_socket != -1) {
        close(data_socket);
    }

    if (control_socket != -1) {
        close(control_socket);
    }

    if (control_listen_socket != -1) {
        close(control_listen_socket);
    }

    if (p_fds[0] != -1) {
        close(p_fds[0]);
    }

    if (p_fds[1] != -1) {
        close(p_fds[1]);
    }

    printf("\033[34m[%d Info]\033[0m DTP: Received SIGTERM, exiting...\n",
           print_pid);
    exit(0); // 正常退出
}

int send_msg(int sockfd, char *sentence) {
    printf("\033[35m[%d RE]\033[0m %s", print_pid, sentence);
    int len = strlen(sentence);
    int p = 0;
    while (p < len) {
        int n = write(sockfd, sentence + p, len - p);
        if (n < 0) {
            printf("\033[31m[%d Error]\033[0m write(): %s(%d)\n", print_pid,
                   strerror(errno), errno);
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
        printf("\033[31m[%d Error]\033[0m getcwd(): %s(%d)\n", print_pid,
               strerror(errno), errno);
        return 1;
    }

    // add "/"
    if (str[strlen(str) - 1] != '/') {
        strcat(str, "/");
    }

    // printf("ori cwd: %s\n", str);

    // remove root_directory
    int len = strlen(root_directory);
    if (strncmp(str, root_directory, len) == 0) {
        strcpy(str, str + len);
    }

    rewrite_path(str);

    return 0;
}

int connect_to(int *sockfd, char *ip, int port) {
    if ((*sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
        printf("\033[31m[%d Error]\033[0m socket(): %s(%d)\n", print_pid,
               strerror(errno), errno);
        return 1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip);

    if (connect(*sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        printf("\033[31m[%d Error]\033[0m connect(): %s(%d)\n", print_pid,
               strerror(errno), errno);
        close(*sockfd);
        *sockfd = -1;
        return 1;
    }

    return 0;
}

int get_msg(int sockfd, char *sentence) {

    while (1) {

        int ret = read_with_timeout(sockfd, sentence);

        if (ret == -1) {
            return -1;
        } else if (ret == 0) { // 关闭
            // 错误在里面已经输出了
            exit_connection();
            return -1;
        } else {
            break;
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

int listen_at(int *sockfd, int port) {

    if ((*sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
        printf("\033[31m[%d Error]\033[0m socket(): %s(%d)\n", print_pid,
               strerror(errno), errno);
        return 1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(*sockfd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        if (errno == EADDRINUSE) {
            printf("\033[31m[%d Error]\033[0m bind(): Port %d is already in "
                   "use.\n",
                   print_pid, port);
            close(*sockfd);
            *sockfd = -1;
            return 2; // 返回2表示端口已被占用
        } else {
            printf("\033[31m[%d Error]\033[0m bind(): %s(%d)\n", print_pid,
                   strerror(errno), errno);
            close(*sockfd);
            *sockfd = -1;
            return 1; // 返回1表示绑定失败
        }
    }

    if (listen(*sockfd, 5) == -1) {
        printf("\033[31m[%d Error]\033[0m listen(): %s(%d)\n", print_pid,
               strerror(errno), errno);
        close(*sockfd);
        *sockfd = -1;
        return 1;
    }

    // printf("listen success %d\n", *sockfd);

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
    // return 1 QUIT
    // return 0 其他消息
    struct request req;
    parse_request(msg, &req);
    printf("\033[33m[%d GET]\033[0m %s; %s\n", print_pid, req.verb,
           req.parameter);

    // reset offset
    if (strcmp(req.verb, "RETR") != 0 && strcmp(req.verb, "STOR") != 0) {
        offset = 0;
    }

    // printf("offset: %lld\n", offset);

    if (strcmp(req.verb, "QUIT") == 0) {
        if (status == PASV) {
            if (data_listen_socket != -1) {
                close(data_listen_socket);
                data_listen_socket = -1;
            }
        }
        send_msg(control_socket, "221 Goodbye.\r\n");
        return 1;
    } else if (strcmp(req.verb, "ABOR") == 0) {
        if (status == PASS) {
            send_msg(control_socket, "225 No transfer to abort.\r\n"); // TODO
            return 0;
        } else if (status == PASV) {
            if (data_listen_socket != -1) {
                close(data_listen_socket);
                data_listen_socket = -1;
            }
            send_msg(control_socket, "225 ABOR command successful.\r\n");
            status = PASS;
            return 0;
        } else if (status == PORT) {
            send_msg(control_socket, "225 ABOR command successful.\r\n");
            status = PASS;
            return 0;
        }
    } else if (strcmp(req.verb, "USER") == 0) {
        if (strcmp(req.parameter, "") == 0) {
            send_msg(control_socket, "501 Please provide a parameter.\r\n");
            return 0;
        }
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
            // send_msg(control_socket, "230 Login successful.\r\n");
            send_msg(control_socket,
                     "230-\r\n230-Welcome to\r\n230- my FTP "
                     "server\r\n230-\r\n230 Login successful.\r\n");
            status = PASS;
            return 0;
        }
    } else if (strcmp(req.verb, "SYST") == 0) {
        send_msg(control_socket, "215 UNIX Type: L8\r\n");
        return 0;
    } else if (strcmp(req.verb, "TYPE") == 0) {
        if (status == PASS || status == PORT || status == PASV) {
            if (strcmp(req.parameter, "") == 0) {
                send_msg(control_socket, "501 Please provide a parameter.\r\n");
                return 0;
            }

            if (strcmp(req.parameter, "I") == 0) {
                send_msg(control_socket, "200 Type set to I.\r\n");
                binary_mode = 1; // on
                return 0;
            } else {
                send_msg(control_socket, "504 Server just support TYPE I.\r\n");
                return 0;
            }
        }
    } else if (strcmp(req.verb, "SIZE") == 0) {

        if (status == PASS || status == PORT || status == PASV) {

            if (strcmp(req.parameter, "") == 0) {
                send_msg(control_socket, "501 Please provide a parameter.\r\n");
                return 0;
            }

            int ret = path_check(req.parameter); // 不能包含../

            if (ret == 1) {
                send_msg(control_socket,
                         "451 Paths cannot contain \"../\".\r\n");
                return 0;
            } else {

                ret = path_convert(req.parameter); // 直接修改吧，因为DTP要看req

                if (ret == 0) {

                    struct stat path_stat;
                    // 使用 stat 函数获取文件状态
                    if (stat(req.parameter, &path_stat) != 0) {
                        printf("\033[31m[%d Error]\033[0m stat(): %s(%d)\n",
                               print_pid, strerror(errno), errno);
                        send_msg(control_socket,
                                 "500 Internal error.\r\n"); // TODO
                        return 0;
                    }

                    // 检查路径是否为一个文件
                    if (!S_ISREG(path_stat.st_mode)) {
                        printf("\033[31m[%d Error]\033[0m %s is not a file\n",
                               print_pid, req.parameter);
                        send_msg(control_socket,
                                 "451 This is not a directory.\r\n");
                        return 0;
                    }

                    char buff[256];
                    sprintf(buff, "213 %ld\r\n", path_stat.st_size);
                    send_msg(control_socket, buff);
                    // printf("size: %ld\n", path_stat.st_size);
                    return 0;

                } else if (ret == 1) {
                    send_msg(control_socket,
                             "451 This is not a directory.\r\n");
                    return 0;
                } else {
                    send_msg(control_socket, "550 Path is not available.\r\n");
                    return 0;
                }
            }
        }

    } else if (strcmp(req.verb, "PWD") == 0) {
        if (status == PASS || status == PORT || status == PASV) {
            char path[256];

            get_cwd(path); // rewrite在这里
            // printf("path: %s\n", path);
            char buff[400];
            sprintf(buff, "257 \"%s\" is the current directory.\r\n", path);
            send_msg(control_socket, buff);
            return 0;
        }
    } else if (strcmp(req.verb, "CWD") == 0) {
        if (strcmp(req.parameter, "") == 0) {
            send_msg(control_socket, "501 Please provide a parameter.\r\n");
            return 0;
        }
        if (status == PASS || status == PORT || status == PASV) {
            char path[256];
            sscanf(req.parameter, "%s", path);

            int ret = path_convert(path);

            if (ret == 0) { // 没有越界
                if (chdir(path) == 0) {
                    send_msg(control_socket,
                             "250 Directory successfully changed.\r\n");
                    return 0;
                } else {
                    send_msg(control_socket,
                             "550 Fail to change the directory.\r\n"); // TODO
                    return 0;
                }
            } else {
                send_msg(control_socket, "550 Path is not available.\r\n");
                return 0;
            }
        }
    } else if (strcmp(req.verb, "MKD") == 0) {
        if (strcmp(req.parameter, "") == 0) {
            send_msg(control_socket, "501 Please provide a parameter.\r\n");
            return 0;
        }
        if (status == PASS || status == PORT || status == PASV) {
            char path[256];
            sscanf(req.parameter, "%s", path);
            // printf("path: %s\n", path);

            if (path[strlen(path) - 1] == '/') { // 去掉最后的/
                path[strlen(path) - 1] = '\0';
            }

            // 要先去掉文件的名称，看看之前的目录存不存在，用convert

            char filename[256];
            char filepath[256];

            basename(path, filename);
            strcpy(filepath, path);
            filepath[strlen(filepath) - strlen(filename)] = '\0';

            if (strcmp(filepath, "") == 0) {
                strcpy(filepath, ".");
            }

            int ret = path_convert(filepath);
            // printf("path: %s\n", filepath);

            if (ret == 0) { // 没有越界
                char real_path[256];
                strcpy(real_path, filepath); // 返回值最后不含/
                strcat(real_path, "/");
                strcat(real_path, filename);
                if (mkdir(real_path, 0777) == 0) {
                    rewrite_path(path); // 用户输入的path
                    char buff[400];
                    sprintf(buff, "257 \"%s\" is created.\r\n", path);
                    send_msg(control_socket, buff);
                    return 0;
                } else {
                    send_msg(control_socket,
                             "550 Failed to create the directory.\r\n"); // TODO
                    return 0;
                }
            } else {
                send_msg(control_socket, "550 Path is not available.\r\n");
                return 0;
            }
        }
    } else if (strcmp(req.verb, "RMD") == 0) {
        if (strcmp(req.parameter, "") == 0) {
            send_msg(control_socket, "501 Please provide a parameter.\r\n");
            return 0;
        }

        if (status == PASS || status == PORT || status == PASV) {
            char path[256];
            sscanf(req.parameter, "%s", path);

            int ret = path_convert(path);

            if (ret == 0) { // 没有越界
                if (rmdir(path) == 0) {
                    send_msg(control_socket, "250 Directory deleted.\r\n");
                    return 0;
                } else {
                    send_msg(control_socket,
                             "550 Failed to remove the directory.\r\n"); // TODO
                    return 0;
                }
            } else {
                send_msg(control_socket, "550 Path is not available.\r\n");
                return 0;
            }
        }
    } else if (strcmp(req.verb, "PORT") == 0) {
        if (strcmp(req.parameter, "") == 0) {
            send_msg(control_socket, "501 Please provide a parameter.\r\n");
            return 0;
        }
        if (status == PASS || status == PORT || status == PASV) {

            if (data_listen_socket != -1) {
                close(data_listen_socket);
                data_listen_socket = -1;
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

            if (data_listen_socket != -1) {
                close(data_listen_socket);
                data_listen_socket = -1;
            }

            // TODO pasv ip
            // printf("data_port: %d\n", data_listen_socket);
            while (1) { // 随机选一个端口
                pasv_mode_info.port =
                    rand() % (MAX_PORT - MIN_PORT + 1) + MIN_PORT;
                // printf("**data_port: %d\n", pasv_mode_info.port);
                if (listen_at(&data_listen_socket, pasv_mode_info.port) == 0) {
                    break;
                }
            }
            // printf("data_port: %d\n", data_listen_socket);

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
        if (strcmp(req.parameter, "") == 0) {
            send_msg(control_socket, "501 Please provide a parameter.\r\n");
            offset = 0;
            return 0;
        }

        if (status == PORT || status == PASV) {

            int ret = path_check(req.parameter);

            if (ret == 1) {
                send_msg(control_socket,
                         "451 Paths cannot contain \"../\".\r\n");
                offset = 0;
                return 0;
            } else {

                ret = path_convert(req.parameter); // 直接修改吧，因为DTP要看req

                if (ret == 0) {
                    // printf("in RETR\n");
                    DTP(req); // DTP中消息已经处理完
                    offset = 0;
                    status = PASS; // 真正用了再expire之前的DTP
                    return 0;
                } else if (ret == 1) {
                    send_msg(control_socket, "451 This is not a file.\r\n");
                    offset = 0;
                    return 0;
                } else {
                    send_msg(control_socket, "550 Path is not available.\r\n");
                    offset = 0;
                    return 0;
                }
            }
        } else {
            send_msg(control_socket, "425 Please use PORT or PASV first.\r\n");
            offset = 0;
            return 0;
        }
    } else if (strcmp(req.verb, "STOR") == 0) {
        if (strcmp(req.parameter, "") == 0) {
            send_msg(control_socket, "501 Please provide a parameter.\r\n");
            offset = 0;
            return 0;
        }

        if (status == PORT || status == PASV) {

            int ret = path_check(req.parameter);

            if (ret == 1) {
                send_msg(control_socket,
                         "451 Paths cannot contain \"../\".\r\n");
                offset = 0;
                return 0;
            } else {

                char path[256];
                sscanf(req.parameter, "%s", path);
                // printf("path: %s\n", path);

                // 要先去掉文件的名称，看看之前的目录存不存在，用convert

                char filename[256];
                char filepath[256];

                basename(path, filename);
                if (strcmp(filename, "") == 0) {
                    send_msg(control_socket, "451 Please provide a file.\r\n");
                    offset = 0;
                    return 0;
                }
                strcpy(filepath, path);
                filepath[strlen(filepath) - strlen(filename)] = '\0';

                if (strcmp(filepath, "") == 0) {
                    strcpy(filepath, ".");
                }

                int ret = path_convert(filepath);
                // printf("path: %s\n", filepath);

                if (ret == 0) { // 目录没有越界, 并且存在
                    char real_path[256];
                    strcpy(real_path, filepath); // 返回值最后不含/
                    strcat(real_path, "/");
                    strcat(real_path, filename);

                    strcpy(req.parameter, real_path); // DTP要看req

                    DTP(req); // DTP中消息已经处理完
                    offset = 0;
                    status = PASS; // 真正用了再expire之前的DTP
                    return 0;
                } else if (ret == 1) {
                    send_msg(control_socket, "451 Path is not exist.\r\n");
                    offset = 0;
                    return 0;
                } else {
                    send_msg(control_socket, "550 Path is not available.\r\n");
                    offset = 0;
                    return 0;
                }
            }
        } else {
            send_msg(control_socket, "425 Please use PORT or PASV first.\r\n");
            offset = 0;
            return 0;
        }
    } else if (strcmp(req.verb, "LIST") == 0 || strcmp(req.verb, "NLST") == 0) {
        if (status == PORT || status == PASV) {

            if (strcmp(req.parameter, "") == 0) {
                strcpy(req.parameter, ".");
            }

            int ret = path_convert(req.parameter); // 直接修改吧，因为DTP要看req

            if (ret == 0) {
                // 看看路径是不是一个文件夹
                struct stat path_stat;
                if (stat(req.parameter, &path_stat) != 0) {
                    printf("\033[31m[%d Error]\033[0m stat(): %s(%d)\n",
                           print_pid, strerror(errno), errno);
                    send_msg(control_socket,
                             "500 Internal error.\r\n"); // TODO
                    return 0;
                }

                if (S_ISDIR(path_stat.st_mode)) {
                    DTP(req);      // DTP中消息已经处理完
                    status = PASS; // 真正用了再expire之前的DTP
                    return 0;
                } else {
                    send_msg(control_socket,
                             "451 This is not a directory.\r\n");
                    return 0;
                }
            } else if (ret == 1) {
                send_msg(control_socket, "451 This is not a directory.\r\n");
                return 0;
            } else {
                send_msg(control_socket, "550 Path is not available.\r\n");
                return 0;
            }

        } else {
            send_msg(control_socket, "425 Please use PORT or PASV first.\r\n");
            return 0;
        }
    } else if (strcmp(req.verb, "REST") == 0) {
        if (strcmp(req.parameter, "") == 0) {
            send_msg(control_socket, "501 Please provide a parameter.\r\n");
            return 0;
        }
        if (status == PORT || status == PASV) {
            offset = atoi(req.parameter);
            if (offset >= 0) {
                send_msg(control_socket, "350 Restart position accepted.\r\n");
                return 0;
            } else {
                send_msg(control_socket, "501 Invalid parameter.\r\n");
                return 0;
            }
        } else {
            send_msg(control_socket, "425 Please use PORT or PASV first.\r\n");
            return 0;
        }

    } else {
        send_msg(control_socket,
                 "502 Unsupport command.\r\n"); // unknown command TODO
        return 0;
    }

    // 溢出的未处理的消息
    if (status == CONNECTED) {
        send_msg(control_socket, "530 Please login with USER.\r\n");
    } else if (status == USER) {
        send_msg(control_socket, "530 Please login with PASS.\r\n");
    } else {
        send_msg(control_socket, "500 retry.\r\n"); // TODO
    }
    return 0;
}

int main(int argc, char *argv[]) {
    binary_mode = 1; // on

    int port = 21;                  // 默认端口
    strcpy(root_directory, "/tmp"); // 用于存储根目录，确保分配足够的空间
    strcpy(pasv_mode_info.ip, "127.0.0.1"); // 默认本机ip

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

    // 可能输入相对路径！！
    char temp[256];
    realpath(root_directory, temp);
    strcpy(root_directory, temp);

    // 去掉最后的'/'
    if (root_directory[strlen(root_directory) - 1] == '/') {
        root_directory[strlen(root_directory) - 1] = '\0';
    }

    if (chdir(root_directory) != 0) { // 不能用change_dir，因为那个会转换路径
        printf("\033[31m[%d Error]\033[0m Cannot change to root directory\n",
               print_pid);
        exit(EXIT_FAILURE);
    }

    if (0 != listen_at(&control_listen_socket, port)) {
        return 1;
    }
    // printf("listenfd: %d\n", control_listen_socket);

    printf("\033[32m[Running]\033[0m port: %d\n", port);
    printf("\033[32m[Running]\033[0m root: %s\n", root_directory);

    while (1) {
        // 为新连接开启新的control socket
        if ((control_socket = accept(control_listen_socket, NULL, NULL)) ==
            -1) {
            printf("\033[31m[%d Error]\033[0m accept(): %s(%d)\n", print_pid,
                   strerror(errno), errno);
            continue;
        }

        int pid = fork();
        if (pid == 0) {
            close(control_listen_socket);
            control_listen_socket = -1;

            print_pid = getpid();

            // 子进程
            // printf("controlfd: %d\n", control_socket);

            status = CONNECTED;
            send_msg(control_socket, "220 Anonymous FTP server ready.\r\n");

            while (1) {
                printf("\033[34m[%d Info]\033[0m Waiting for next request.\n",
                       print_pid);
                char msg[SENTENCE_LEN];
                get_msg(control_socket, msg);

                // printf("msg: %s\n", msg);

                if (handle_request(msg) == 1) {
                    break;
                }
            }

            close(control_socket);
            control_socket = -1;
            exit(0);
        } else if (pid > 0) {
            close(control_socket); // 记得关！！！
            control_socket = -1;
        } else {
            printf("\033[31m[%d Error]\033[0m fork(): %s (%d)\n", print_pid,
                   strerror(errno), errno);
            send_msg(control_socket, "500 Internal error.\r\n");
        }
    }

    close(control_listen_socket);
    control_listen_socket = -1;

    exit_connection();
}