#include "client.h"

enum user_status status;

int control_listen_socket = -1;
int control_socket = -1;
int data_listen_socket = -1;
int data_socket = -1;

int dtp_pid = -1;

int p_fds[2] = {-1, -1};

long long offset = 0;
long long size = 0; // 慎用，不会刷新

int size_mode = 0; // 不使用进度条

int repress_output = 0; // 不抑制输出

int binary_mode;

struct port_mode_info_s port_mode_info;
struct pasv_mode_info_s pasv_mode_info;

FILE *file = NULL;
FILE *pfile = NULL;

void exit_connection() {
    if (dtp_pid != -1 && kill(dtp_pid, 0) == 0) {
        kill(
            dtp_pid,
            SIGTERM); // 这可以，因为仅有的存在子进程的情况已经确保收到SIGTERM会退出
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

    if (file != NULL) {
        fclose(file);
        file = NULL;
    }

    if (pfile != NULL) {
        pclose(pfile);
        pfile = NULL;
    }

    if (p_fds[0] != -1) {
        close(p_fds[0]);
    }

    if (p_fds[1] != -1) {
        close(p_fds[1]);
    }

    exit(0);
}

int read_with_timeout(int sockfd, char *ch) { // 为DTP设计??

    // RETURN 1: close, TIMEOUT

    struct pollfd fds[1];
    int ret;

    // Set up the poll structure
    fds[0].fd = sockfd;
    fds[0].events = POLLIN; // We are waiting for input (readable data)

    // Poll with a timeout of 1 minute
    ret = poll(fds, 1, TIMEOUT_MS_READ);

    if (ret == -1) {
        printf("\033[31m[Error]\033[0m poll(): %s(%d)\n", strerror(errno),
               errno);
        return -1;
    } else if (ret == 0) {
        // Timeout occurred
        printf("\033[31m[Error]\033[0m Timeout after %d minute waiting for "
               "get msg.\n",
               TIMEOUT_MS_READ / 60000);
        return 1; // Timeout, exit the function
    } else {
        int n = read(sockfd, ch, 1);

        if (n < 0) {
            printf("\033[31m[Error]\033[0m read(): %s(%d)\n", strerror(errno),
                   errno);
            return -1;
        } else if (n == 0) { // close
            return 1;
        }
    }

    return 0; // Continue reading
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
        printf("\033[31m[Error]\033[0m poll(): %s(%d)\n", strerror(errno),
               errno);
        return -1;
    } else if (ret == 0) {
        // Timeout occurred
        printf("\033[31m[Error]\033[0m Timeout after %d minute waiting for "
               "a connection.\n",
               TIMEOUT_MS_ACCEPT / 60000);
        return -1;
    } else {
        // There is a connection to accept
        if (fds[0].revents & POLLIN) {
            data_socket = accept(data_listen_socket, NULL, NULL);
            if (data_socket == -1) {
                printf("\033[31m[Error]\033[0m accept(): %s(%d)\n",
                       strerror(errno), errno);
                return -1;
            }
        }
    }

    return data_socket;
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

void handle_abor_main(int sig) { // 主进程使用

    if (dtp_pid == -1 || kill(dtp_pid, 0) != 0) { // 没有子进程
        return;
    }

    kill(dtp_pid, SIGTERM); // 让子进程进入等待
    send_msg(control_socket, "ABOR\r\n");
    // printf("ABOR sent.\n");

    char msg[SENTENCE_LEN];
    // printf("waiting.\n");
    get_msg(control_socket,
            msg); // 如果超时怎么办，超时了就当对面不能响应

    // 只有此时的get_msg是同时存在子进程，并且此时子进程可以收听SIGTERM而直接退出

    if (msg[0] == '4') {
        // printf("ABOR success\n");
        int pid_signal = 0; // 继续执行退出
        write(p_fds[1], &pid_signal, sizeof(pid_signal));
    } else {
        // printf("ABOR fail\n");
        int pid_signal = 1; // 不退出
        write(p_fds[1], &pid_signal, sizeof(pid_signal));
    }

    // 然后返回waitpid位置，根据子进程的情况决定是否退出
}

void close_DTP(int sig) {
    // 安全退出DTP
    // printf("Child process: Received SIGTERM again, exiting...\n");

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

    if (file != NULL) {
        fclose(file);
        file = NULL;
    }

    if (pfile != NULL) {
        pclose(pfile);
        pfile = NULL;
    }

    if (file != NULL) {
        fclose(file);
        file = NULL;
    }

    if (pfile != NULL) {
        pclose(pfile);
        pfile = NULL;
    }

    if (p_fds[0] != -1) {
        close(p_fds[0]);
    }

    if (p_fds[1] != -1) {
        close(p_fds[1]);
    }

    // printf("Child process: ok\n");

    exit(2);
}

void handle_abor_DTP(int sig) {
    // 处理 SIGTERM 信号，执行清理操作

    // T此时更换SIGTERM的响应，如果此时继续收到SIGTERM，就直接安全退出而不等待主进程发送消息
    signal(SIGTERM, close_DTP);

    printf("\n");
    // printf("Child process: Received SIGTERM, hanging...\n");

    // hang up
    // 等待主进程回复

    int ret;
    read(p_fds[0], &ret, sizeof(ret));

    if (ret == 0) { // 继续退出

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

        if (file != NULL) {
            fclose(file);
            file = NULL;
        }

        if (pfile != NULL) {
            pclose(pfile);
            pfile = NULL;
        }

        if (file != NULL) {
            fclose(file);
            file = NULL;
        }

        if (pfile != NULL) {
            pclose(pfile);
            pfile = NULL;
        }

        if (p_fds[0] != -1) {
            close(p_fds[0]);
        }

        if (p_fds[1] != -1) {
            close(p_fds[1]);
        }

        // printf("Child process: ok\n");

        exit(2);
    }

    // printf("back to normal\n");
    signal(SIGTERM, handle_abor_DTP);

    return;
}

int DTP(struct request req) {
    // DTP遇到问题就退出，主进程等server发消息
    // 主进程不需要给DTP发消息，DTP自己处理
    // 检查一下是不是真的会退出

    // 返回主进程0成功
    // 返回主进程1失败

    // 子进程返回2被迫退出
    // 子进程返回1错误退出
    // 子进程返回0正常退出

    if (pipe(p_fds) == -1) {
        printf("\033[31m[Error]\033[0m pipe(): %s(%d)\n", strerror(errno),
               errno);
        char msg[SENTENCE_LEN];
        get_msg(control_socket, msg); // 等待错误消息
        return 1;
    }

    dtp_pid = fork();
    if (dtp_pid == 0) {          // 创建DTP
        signal(SIGINT, SIG_IGN); // 停止监听键盘
        signal(SIGTERM, handle_abor_DTP);

        // 正常传输退出 0
        // 异常退出 1
        // ctrl + c退出 2
        close(control_socket);
        control_socket = -1;

        close(p_fds[1]); // 关闭写，子进程只需要读取
        p_fds[1] = -1;

        if (status == PORT) {

            if ((data_socket = accept_with_timeout(data_listen_socket)) == -1) {
                close(data_listen_socket);
                data_listen_socket = -1;
                close(p_fds[0]);
                p_fds[0] = -1;
                exit(1);
            } else {
                close(data_listen_socket);
                data_listen_socket = -1;
            }

        } else if (status == PASV) {
            if (0 != connect_to(&data_socket, pasv_mode_info.ip,
                                pasv_mode_info.port)) {
                close(p_fds[0]);
                p_fds[0] = -1;
                exit(1);
            }
        }

        // 开始传输

        if (strcmp(req.verb, "RETR") == 0) {

            // printf("get_file %s\n", req.parameter);

            if (file == NULL) { // 如果没有别的地方打开file
                char filename[256];
                basename(req.parameter, filename);
                if (offset != 0) {
                    file = fopen(filename, "ab");
                } else {
                    file = fopen(filename, "wb");
                }
            }

            if (file == NULL) {
                printf("\033[31m[Error]\033[0m fopen(): %s(%d)\n",
                       strerror(errno), errno);
                close(data_socket);
                data_socket = -1;
                close(p_fds[0]);
                p_fds[0] = -1;
                exit(1);
            }

            if (offset != 0) {
                if (fseek(file, offset, SEEK_SET) != 0) {
                    printf("\033[31m[Error]\033[0m fseek(): %s(%d)\n",
                           strerror(errno), errno);
                    close(data_socket);
                    data_socket = -1;
                    close(p_fds[0]);
                    p_fds[0] = -1;
                    exit(1);
                }
            }

            char buff[256];
            int n;
            // printf("retring\n");

            size_t total_transferred = offset;
            size_t update_interval = 1024 * 500; // 每传输500KB更新一次进度
            size_t next_update = total_transferred + update_interval;
            while ((n = read(data_socket, buff, 256)) > 0) { // 从socket读入file
                fwrite(buff, 1, n, file);
                total_transferred += n;

                if (total_transferred >= next_update) {
                    if (size_mode == 0) { // 不使用进度条
                        printf("\r\033[34mTransferred: %llu bytes\033[0m",
                               total_transferred - offset);
                        fflush(stdout);
                        next_update += update_interval; // 更新下一个展示点
                    } else {
                        printf(
                            "\r\033[34mTransferred: %llu bytes, %.2f%%\033[0m",
                            total_transferred - offset,
                            (double)total_transferred / size * 100);
                        fflush(stdout);
                        next_update += update_interval; // 更新下一个展示点
                    }
                }
            }

            if (n == -1) {
                printf("\033[31m[Error]\033[0m read(): %s(%d)\n",
                       strerror(errno), errno);
                close(data_socket);
                data_socket = -1;
                close(p_fds[0]);
                p_fds[0] = -1;
                exit(1);
            }

            fflush(stdout);
            printf("\r\033[32mDone! Total transferred: %llu bytes\033[0m\n",
                   total_transferred - offset);

            // pipe可以传输当前传递了多少

            fclose(file);
            file = NULL;

        } else if (strcmp(req.verb, "STOR") == 0) {

            if (file == NULL) { // 如果没有别的地方打开file
                char filename[256];
                basename(req.parameter, filename);
                // printf("filename: %s\n", filename);
                file = fopen(filename, "rb"); // 保存到当前目录
            }

            // file = fopen(req.parameter, "rb"); //parameter是server的路径
            if (file == NULL) {
                printf("\033[31m[Error]\033[0m fopen(): %s(%d)\n",
                       strerror(errno), errno);
                close(data_socket);
                data_socket = -1;
                close(p_fds[0]);
                p_fds[0] = -1;
                exit(1);
            }

            if (offset != 0) {
                if (fseek(file, offset, SEEK_SET) != 0) {
                    printf("\033[31m[Error]\033[0m fseek(): %s(%d)\n",
                           strerror(errno), errno);
                    close(data_socket);
                    data_socket = -1;
                    close(p_fds[0]);
                    p_fds[0] = -1;
                    exit(1);
                }
            }

            char buff[256];
            int n;
            size_t total_transferred = offset;
            size_t update_interval = 1024 * 500; // 每传输500KB更新一次进度
            size_t next_update = total_transferred + update_interval;
            while ((n = fread(buff, 1, 256, file)) > 0) { // 从file读入sockt

                if (write(data_socket, buff, n) == -1) {
                    fclose(file);
                    file = NULL;
                    printf("\033[31m[Error]\033[0m write(): %s(%d)\n",
                           strerror(errno), errno);
                    close(data_socket);
                    data_socket = -1;
                    close(p_fds[0]);
                    p_fds[0] = -1;
                    exit(1);
                }

                total_transferred += n;

                if (total_transferred >= next_update) {
                    if (size_mode == 0) {
                        printf("\r\033[34mTransferred: %llu bytes\033[0m",
                               total_transferred - offset);
                        fflush(stdout);
                        next_update += update_interval; // 更新下一个展示点
                    } else {
                        printf(
                            "\r\033[34mTransferred: %llu bytes, %.2f%%\033[0m",
                            total_transferred - offset,
                            (double)total_transferred / size * 100);
                        fflush(stdout);
                        next_update += update_interval; // 更新下一个展示点
                    }
                }
            }

            // printf("send file success\n");
            fflush(stdout);
            printf("\r\033[32mDone! Total transferred: %llu bytes\033[0m\n",
                   total_transferred - offset);
            // fflush(stdout);

            fclose(file);
            file = NULL;

        } else if (strcmp(req.verb, "LIST") ==
                   0) { // 我怎么知道什么时候结束, 不知道结束符，只能等对方关闭
            char buffer[1024];
            int n;

            while ((n = recv(data_socket, buffer, sizeof(buffer) - 1, 0)) > 0) {
                buffer[n] = '\0'; // 确保缓冲区是一个有效的字符串
                printf("%s", buffer); // 打印接收到的消息
            }

            if (n == -1) {
                // perror("recv");
                printf("\033[31m[Error]\033[0m recv(): %s(%d)\n",
                       strerror(errno), errno);
                close(data_socket);
                data_socket = -1;
                close(p_fds[0]);
                p_fds[0] = -1;
                exit(1);
            }
        }
        // 正常退出
        close(data_socket);
        data_socket = -1;
        close(p_fds[0]);
        p_fds[0] = -1;
        exit(0);
    } else if (dtp_pid > 0) {
        signal(SIGINT, handle_abor_main);

        close(p_fds[0]); // 关闭读，主进程只需要写
        p_fds[0] = -1;

        int pid_signal;
        waitpid(dtp_pid, &pid_signal, 0); // 等待子进程结束

        close(p_fds[1]);
        p_fds[1] = -1;

        signal(SIGINT, SIG_DFL);

        char msg[SENTENCE_LEN];

        if (WEXITSTATUS(pid_signal) == 2) { // 被退出  注意是WEXITSTATUS
            get_msg(control_socket, msg);   // 226 正确ABOR
            return 1;
        } else if (WEXITSTATUS(pid_signal) == 0) { // 正确推出
            get_msg(control_socket, msg);          // 150
            return 0;
        } else { // 错误退出
            get_msg(control_socket, msg);
            return 1;
        }
    } else {
        printf("\033[31m[Error]\033[0m fork(): %s(%d)\n", strerror(errno),
               errno);
        char msg[SENTENCE_LEN];
        get_msg(control_socket, msg); // 等待错误消息
        return 1;
    }

    return 0;
}

int send_msg(int sockfd, char *sentence) {
    int len = strlen(sentence);
    int p = 0;
    while (p < len) {
        int n = write(sockfd, sentence + p, len - p);
        if (n < 0) { // close
            printf("\033[31m[Error]\033[0m write(): %s(%d)\n", strerror(errno),
                   errno);
            return 1;
        } else {
            p += n;
        }
    }
    return 0;
}

int connect_to(int *sockfd, char *ip, int port) {
    if ((*sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
        printf("\033[31m[Error]\033[0m socket(): %s(%d)\n", strerror(errno),
               errno);
        return 1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip);

    if (connect(*sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        printf("\033[31m[Error]\033[0m connect(): %s(%d)\n", strerror(errno),
               errno);
        close(*sockfd);
        *sockfd = -1;
        return 1;
    }

    return 0;
}

int get_msg(int sockfd,
            char *sentence) { // 只会在主进程，以及没有子进程存在的时候调用
    // 避免一下进来很多条消息

    int start = 0, end = 0;

    // int p = 0;

    // 定义正则表达式
    const char *last_line_pattern = "^[0-9]{3} ";
    regex_t regex;
    int reti;

    // 编译正则表达式
    reti = regcomp(&regex, last_line_pattern, REG_EXTENDED);
    if (reti) {
        printf("\033[31m[Error]\033[0m Could not compile regex\n");
        return 0;
    }

    while (1) { // 读完整个回复

        while (1) { // 读完句子
            char ch;

            int ret = read_with_timeout(sockfd, &ch);

            if (ret == -1) {
                return -1;
            } else if (ret == 1) { // close
                printf("\033[31mConnection closed by the "
                       "host.\033[0m\n");
                exit_connection();
            } else {
                // printf("%c", ch);
                //  将读取到的字符存储到句子中
                sentence[end++] = ch;

                // 检查是否是\r\n
                if (end >= start + 2 && sentence[end - 2] == '\r' &&
                    sentence[end - 1] == '\n') {
                    // 最后一句话再结束
                    // sentence[p - 2] = '\0'; // 替换最后的\r字符为结束符
                    // p--; // 退回到结束符前一个位置
                    break;
                }

                // 确保不会超出 buffer 大小
                if (end >= SENTENCE_LEN - 1) {
                    printf("\033[31m[Error]\033[0m buffer overflow\n");
                    return -1; // 防止溢出
                }
            }
        }

        // 检查是不是最后一句

        char new_sentence[SENTENCE_LEN];
        memcpy(new_sentence, sentence + start, end - start);
        // strcpy(new_sentence, sentence + start); 没有加\0

        // 执行正则表达式匹配
        reti = regexec(&regex, new_sentence, 0, NULL, 0);

        if (reti == 0) {          // 匹配，是结尾
            sentence[end] = '\0'; // 最后自带换行符
            break;
        } else {
            start = end; // end是下一个要读取进来的位置
        }
    }
    if (repress_output == 0) {
        printf("%s", sentence);
        fflush(stdout);
    }
    regfree(&regex); // 释放正则表达式

    return 0;
}

int listen_at(int *sockfd, int port) {

    if ((*sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
        printf("\033[31m[Error]\033[0m socket(): %s(%d)\n", strerror(errno),
               errno);
        return 1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(*sockfd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        if (errno == EADDRINUSE) {
            printf("\033[31m[Error]\033[0m bind(): Port %d is already in "
                   "use.\n",
                   port);
            close(*sockfd);
            *sockfd = -1;
            return 2; // 返回2表示端口已被占用
        } else {
            printf("\033[31m[Error]\033[0m bind(): %s(%d)\n", strerror(errno),
                   errno);
            close(*sockfd);
            *sockfd = -1;
            return 1; // 返回1表示绑定失败
        }
    }

    if (listen(*sockfd, 5) == -1) {
        printf("\033[31m[Error]\033[0m listen(): %s(%d)\n", strerror(errno),
               errno);
        close(*sockfd);
        *sockfd = -1;
        return 1;
    }

    // printf("listen success %d\n", *sockfd);

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
int handle_request(char *sentence) { // 成功与否还是要返回一下
    // 成功了返回0
    // 失败了返回1
    // 退出-1
    struct request req;
    char msg[SENTENCE_LEN];
    parse_request(sentence, &req);
    // printf("** verb: %s\n", req.verb);
    // printf("** para: %s\n", req.parameter);
    // printf("** len: %ld\n", strlen(req.parameter));

    // reset offset
    if (strcmp(req.verb, "RETR") != 0 && strcmp(req.verb, "STOR") != 0) {
        offset = 0;
    }

    // printf("offset: %lld\n", offset);

    if (strcmp(req.verb, "QUIT") == 0 || strcmp(req.verb, "ABOR") == 0) {
        send_msg(control_socket, sentence);
        get_msg(control_socket, msg);
        return -1;
        //} else if (strcmp(req.verb, "ABOR") == 0) {
        //    send_msg(control_socket, sentence);
        //    get_msg(control_socket, msg);
    } else if (strcmp(req.verb, "USER") == 0) {
        send_msg(control_socket, sentence);
        get_msg(control_socket, msg);
    } else if (strcmp(req.verb, "PASS") == 0) {
        send_msg(control_socket, sentence);
        get_msg(control_socket, msg);
    } else if (strcmp(req.verb, "SYST") == 0 || strcmp(req.verb, "TYPE") == 0 ||
               strcmp(req.verb, "PWD") == 0 || strcmp(req.verb, "CWD") == 0 ||
               strcmp(req.verb, "MKD") == 0 || strcmp(req.verb, "RMD") == 0) {
        send_msg(control_socket, sentence);
        get_msg(control_socket, msg);
    } else if (strcmp(req.verb, "PORT") == 0) {

        if (data_listen_socket != -1) {
            close(data_listen_socket);
            data_listen_socket = -1;
        }

        if (strcmp(req.parameter, "") == 0) { // 未指定

            while (1) { // 随机选一个端口
                port_mode_info.port =
                    rand() % (MAX_PORT - MIN_PORT + 1) + MIN_PORT;
                // printf("**data_port: %d\n", port);
                if (listen_at(&data_listen_socket, port_mode_info.port) == 0) {
                    break;
                }
            }

            int p1, p2, p3, p4;
            sscanf(port_mode_info.ip, "%d.%d.%d.%d", &p1, &p2, &p3, &p4);

            char buff[256];
            sprintf(buff, "PORT %d,%d,%d,%d,%d,%d\r\n", p1, p2, p3, p4,
                    port_mode_info.port / 256, port_mode_info.port % 256);

            send_msg(control_socket, buff);
            get_msg(control_socket, msg);

            struct response res;
            parse_response(msg, &res);

            if (res.code[0] == '2') {
                status = PORT;
                return 0;
            } else {
                close(data_listen_socket);
                data_listen_socket = -1;
                return 1;
            }

        } else { // 指定了

            int ip1, ip2, ip3, ip4, port1, port2;
            sscanf(req.parameter, "%d,%d,%d,%d,%d,%d", &ip1, &ip2, &ip3, &ip4,
                   &port1, &port2);

            if (0 != listen_at(&data_listen_socket, port1 * 256 + port2)) {
                printf("\033[31m[Error]\033[0m PORT already in use, please "
                       "use another.\n");
                return 0;
            }

            send_msg(control_socket, sentence);
            get_msg(control_socket, msg);

            struct response res;
            parse_response(msg, &res);

            if (res.code[0] == '2') {
                status = PORT;
                return 0;
            } else {
                close(data_listen_socket);
                data_listen_socket = -1;
                return 1;
            }
        }

    } else if (strcmp(req.verb, "PASV") == 0) {
        if (data_listen_socket != -1) {
            close(data_listen_socket);
            data_listen_socket = -1;
        }

        send_msg(control_socket, sentence);
        get_msg(control_socket, msg);

        struct response res;
        parse_response(msg, &res);

        if (res.code[0] == '2') {
            int p1, p2, p3, p4, p5, p6;
            sscanf(res.message[0], "%*[^0-9]%d,%d,%d,%d,%d,%d%*[^\n]", &p1, &p2,
                   &p3, &p4, &p5, &p6);
            sprintf(pasv_mode_info.ip, "%d.%d.%d.%d", p1, p2, p3, p4);
            pasv_mode_info.port = p5 * 256 + p6;

            status = PASV;
            return 0;
        }

    } else if (strcmp(req.verb, "RETR") == 0 || strcmp(req.verb, "STOR") == 0 ||
               strcmp(req.verb, "LIST") == 0) {

        send_msg(control_socket, sentence);
        char msg[SENTENCE_LEN];
        get_msg(control_socket, msg);
        struct response res;
        parse_response(msg, &res);
        int ret = 1;
        if (res.code[0] == '1') { // 可以连接
            ret = DTP(req);
            size_mode = 0; // 默认关闭
        }

        if (strcmp(req.verb, "RETR") == 0 || strcmp(req.verb, "STOR") == 0) {
            offset = 0;
        }

        return ret;

        // status = PASS; 这样会导致下次传输的时候不知道用PORT还是PASV
    } else if (strcmp(req.verb, "REST") == 0) {
        send_msg(control_socket, sentence);
        get_msg(control_socket, msg);
        struct response res;
        parse_response(msg, &res);

        if (res.code[0] == '3') {
            sscanf(req.parameter, "%lld", &offset);
            // printf("Restarting at %lld\n", offset);
            return 0;
        } else {
            offset = 0;
            // printf("Restarting fail\n");
            return 1;
        }
    } else if (strcmp(req.verb, "SIZE") == 0) {
        send_msg(control_socket, sentence);
        get_msg(control_socket, msg);

        struct response res;
        parse_response(msg, &res);

        if (res.code[0] == '2') {
            struct response res;
            parse_response(msg, &res);
            int ret = sscanf(res.message[0], "%*[^0-9]%lld", &size);
            if (ret != 1) { // 全部都是数字
                sscanf(res.message[0], "%lld", &size);
            }
            // printf("msg size: %ld\n", strlen(res.message[0]));
            // printf("msg: '%s'\n", res.message[0]);
            // printf("size: %lld\n", size);
            return 0;
        } else {
            size = 0;
            return 1;
        }
    } else if (strcmp(req.verb, "get") == 0) {

        repress_output = 1;

        char local_path[256];
        char remote_path[256];

        if (sscanf(req.parameter, "%s %s", remote_path, local_path) != 2) {
            printf("\033[33mUsage: get <remote_path> "
                   "<loacal_path>\033[0m\n");
            repress_output = 0;
            return 1;
        }

        // 先获取本地文件大小

        long long local_size = 0;

        struct stat path_stat;
        // 使用 stat 函数获取文件状态
        // 需要根据本地是否存在文件来决定是否需要ab
        if (stat(local_path, &path_stat) != 0) {
            // 文件不存在时
            // printf("\033[31m[Error]\033[0m stat(): %s(%d)\n",
            // strerror(errno), errno); printf("continue\n");

            local_size = 0;

            file = fopen(local_path, "wb");

            if (file == NULL) {
                printf("\033[31m[Error]\033[0m fopen(): %s(%d)\n",
                       strerror(errno), errno);
                repress_output = 0;
                return 1;
            }
        } else {
            if (!S_ISREG(path_stat.st_mode)) {
                printf("\033[31m[Error]\033[0m %s is not a file\n",
                       req.parameter);
                repress_output = 0;
                return 1;
            } else {                            // 文件存在
                local_size = path_stat.st_size; // 用来RETR

                file = fopen(local_path, "ab");

                if (file == NULL) {
                    printf("\033[31m[Error]\033[0m fopen(): %s(%d)\n",
                           strerror(errno), errno);
                    repress_output = 0;
                    return 1;
                }
            }
        }

        // 尝试获取SIZE，打开进度条
        int ret;

        char buff[500];

        sprintf(buff, "SIZE %s\r\n", remote_path);
        ret = handle_request(buff);
        if (ret == 1) {
            // printf("SIZE error, back to normal.\n");
        } else {
            // printf("SIZE success. %lld\n", size);
            size_mode = 1; // 用来显示进度条
        }

        sprintf(buff, "PASV\r\n");
        ret = handle_request(buff);
        if (ret == 1) {
            // printf("PASV error.\n");
            repress_output = 0;
            return 1;
        }

        if (local_size != 0) {
            sprintf(buff, "REST %lld\r\n", local_size);
            ret = handle_request(buff);
            if (ret == 1) {
                // printf("REST error, back to normal\n");
            } else {
                printf("\033[33mRestarting at %lld\033[0m\n", local_size);
            }
        }

        sprintf(buff, "RETR %s\r\n", remote_path); // 里面处理了offset
        ret = handle_request(buff);

        repress_output = 0;

    } else if (strcmp(req.verb, "put") == 0) {

        repress_output = 1;

        char local_path[256];
        char remote_path[256];

        if (sscanf(req.parameter, "%s %s", local_path, remote_path) != 2) {
            printf("\033[33mUsage: put <local_path> "
                   "<remote_path>\033[0m\n");
            repress_output = 0;
            return 1;
        }

        file = fopen(local_path, "rb");

        if (file == NULL) {
            printf("\033[31m[Error]\033[0m fopen(): %s(%d)\n", strerror(errno),
                   errno);
            repress_output = 0;
            return 1;
        }

        int ret;

        char buff[500];
        sprintf(buff, "SIZE %s\r\n", remote_path);
        ret = handle_request(buff);
        if (ret == 1) {
            // printf("SIZE error, back to normal.\n");
        } else {
            // printf("SIZE success. %lld\n", size); // 用来RETR
        }

        // put的size是本底的

        sprintf(buff, "PASV\r\n");
        ret = handle_request(buff);
        if (ret == 1) {
            // printf("PASV error.\n");
            repress_output = 0;
            return 1;
        }

        if (size != 0) {
            sprintf(buff, "REST %lld\r\n", size);
            ret = handle_request(buff);
            if (ret == 1) {
                // printf("REST error, back to normal\n");
            } else {
                // printf("REST success.\n");
                printf("\033[33mRestarting at %lld\n\033[0m", size);
            }
        }

        // 获取local大小
        struct stat path_stat;
        if (stat(local_path, &path_stat) != 0) {
            // 文件不存在时
            printf("\033[31m[Error]\033[0m stat(): %s(%d)\n", strerror(errno),
                   errno);
            repress_output = 0;
            return 1;
        } else {
            if (!S_ISREG(path_stat.st_mode)) {
                printf("\033[31m[Error]\033[0m %s is not a file\n",
                       req.parameter);
                repress_output = 0;
                return 1;
            } else { // 文件存在
                size = path_stat.st_size;
                size_mode = 1; // 用来显示进度条
            }
        }

        sprintf(buff, "STOR %s\r\n", remote_path); // 里面处理了offset
        ret = handle_request(buff);

        repress_output = 0;

    } else {
        printf("\033[31mUnsupport command\033[0m\n");
    }

    return 0;
}

int main(int argc, char *argv[]) {

    strcpy(port_mode_info.ip, "127.0.0.1"); // 本机ip

    char server_ip[16] = "127.0.0.1";
    int server_port = 21;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-ip") == 0) {
            if (i + 1 < argc) {
                strcpy(server_ip, argv[i + 1]); // 获取根目录字符串
                i++;                            // 跳过参数
            } else {
                fprintf(stderr, "Error: -ip requires an argument\n");
                exit(EXIT_FAILURE);
            }
        } else if (strcmp(argv[i], "-port") == 0) {
            if (i + 1 < argc) {
                server_port = atoi(argv[i + 1]); // 将字符串转换为整数
                i++;                             // 跳过参数
            } else {
                fprintf(stderr, "Error: -port requires an argument\n");
                exit(EXIT_FAILURE);
            }
        } else {
            fprintf(stderr, "Usage: %s -ip <x.x.x.x> -port <port>\n", argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    strcpy(pasv_mode_info.ip, server_ip); // 不过PASV过程中会记录server IP

    // printf("ip: %s\n", server_ip);
    // printf("port: %d\n", server_port);

    if (0 != connect_to(&control_socket, server_ip, server_port)) {
        // printf("cannot connect to the server\n");
        return 1;
    }

    // 欢迎信息
    char msg[SENTENCE_LEN];
    get_msg(control_socket, msg);

    status = CONNECTED;

    while (1) {
        // 用户输入
        char sentence[SENTENCE_LEN];

        // printf("myftp> ");

        fgets(sentence, SENTENCE_LEN - 1, stdin);
        int len = strlen(sentence);
        sentence[len - 1] = '\r';
        sentence[len] = '\n';
        sentence[len + 1] = '\0';

        // 进入循环，同时接受responce，直接返回结果是否正确
        if (-1 == handle_request(sentence)) {
            break;
        }

        // printf("status: %d\n",status);
    }

    close(control_socket);
    control_socket = -1;

    exit_connection();
}