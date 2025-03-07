#include <arpa/inet.h>  /* IP address conversion stuff */
#include <netdb.h>      /* gethostbyname */
#include <netinet/in.h> /* INET constants and stuff */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h> /* socket specific definitions */
#include <sys/types.h>  /* system data type definitions */
#include <unistd.h>     /* defines STDIN_FILENO, system calls,etc */

#define MAXBUF 10 * 1024

int main() {

    int sk;
    struct sockaddr_in server;
    struct hostent *hp;
    char buf[MAXBUF];

    /* create a socket
       IP protocol family (PF_INET)
       UDP (SOCK_DGRAM)
    */

    if ((sk = socket(PF_INET, SOCK_DGRAM, 0)) < 0) {
        printf("Problem creating socket\n");
        exit(1);
    }

    server.sin_family = AF_INET;
    hp = gethostbyname("localhost"); // 获取localhost的ip

    /* copy the IP address into the sockaddr
       It is already in network byte order
    */

    // server.sin_family 协议
    // server.sin_port 端口
    // server.sin_addr.s_addr ip地址

    memcpy(&server.sin_addr.s_addr, hp->h_addr, hp->h_length);

    /* establish the server port number - we must use network byte order! */
    server.sin_port = htons(9876);

    /* read everything possible */
    // fgets(buf, MAXBUF, stdin);
    // size_t buf_len = strlen(buf);

    /* send it to the echo server */

    // int n_sent = sendto(sk, buf, buf_len, 0,
    //                 (struct sockaddr*) &server, sizeof(server));

    for (int i = 0; i <= 50; i++) {

        int buf_len = sprintf(buf, "%d", i);

        // printf("%s\n",buf);

        int n_sent = sendto(sk, buf, buf_len, 0, (struct sockaddr *)&server,
                            sizeof(server));

        if (n_sent < 0) {
            perror("Problem sending data");
            exit(1);
        }

        if (n_sent != buf_len) {
            printf("Sendto sent %d bytes\n", n_sent);
        }
    }

    // sk是socket
    // sendto已经隐式分配了端口

    // printf("sending finished\n");

    for (int i = 0; i <= 50; i++) {
        /* Wait for a reply (from anyone) */
        int n_read = recvfrom(sk, buf, MAXBUF, 0, NULL, NULL);
        if (n_read < 0) {
            perror("Problem in recvfrom");
            exit(1);
        }

        /* send what we got back to stdout */
        if (write(STDOUT_FILENO, buf, n_read) <
            0) { // n_read是接收到的数据的长度, 不会溢出
            perror("Problem writing to stdout");
            exit(1);
        }
        printf("\n");
    }
    return 0;
}
