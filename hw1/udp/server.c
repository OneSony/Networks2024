#include <arpa/inet.h> /* IP address conversion stuff */
#include <ctype.h>
#include <netdb.h>      /* gethostbyname */
#include <netinet/in.h> /* INET constants and stuff */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h> /* socket specific definitions */
#include <sys/types.h>  /* system data type definitions */
#include <unistd.h>     /* defines STDIN_FILENO, system calls,etc */

#define MAXBUF 1024 * 1024

long long seq_num = 0;

void uppercase(char *p) {
    for (; *p; ++p)
        *p = toupper(*p);
}

void echo(int sd) {
    char bufin[MAXBUF];
    struct sockaddr_in remote;

    /* need to know how big address struct is, len must be set before the
       call to recvfrom!!! */
    socklen_t len = sizeof(remote);

    while (1) {
        /* read a datagram from the socket (put result in bufin) */
        int n =
            recvfrom(sd, bufin, MAXBUF, 0, (struct sockaddr *)&remote, &len);

        if (n < 0) {
            perror("Error receiving data");
        } else {
            // uppercase(bufin);
            /* Got something, just send it back */
            printf("get n: %d\n", n);
            bufin[n] =
                '\0'; // recvfrom返回的数据不是以'\0'结尾的，所以要手动加上
            printf("get: %s\n", bufin);
            printf("seq_num: %lld\n", seq_num);

            char seq_msg[MAXBUF];
            sprintf(seq_msg, "%lld", seq_num);
            strcat(seq_msg, " ");
            strcat(seq_msg, bufin);
            printf("send: %s\n", seq_msg);
            sendto(sd, seq_msg, strlen(seq_msg), 0, (struct sockaddr *)&remote,
                   len);
            seq_num++;
        }
    }
}

/* server main routine */

int main() {
    int ld;
    struct sockaddr_in skaddr;
    socklen_t length;

    /* create a socket
       IP protocol family (PF_INET)
       UDP protocol (SOCK_DGRAM)
    */

    if ((ld = socket(PF_INET, SOCK_DGRAM, 0)) < 0) {
        printf("Problem creating socket\n");
        exit(1);
    }

    /* establish our address
       address family is AF_INET
       our IP address is INADDR_ANY (any of our IP addresses)
       the port number is 9876
    */

    skaddr.sin_family = AF_INET;
    skaddr.sin_addr.s_addr = htonl(INADDR_ANY); // 收听所有的ip地址
    skaddr.sin_port = htons(9876);

    if (bind(ld, (struct sockaddr *)&skaddr, sizeof(skaddr)) < 0) {
        printf("Problem binding\n");
        exit(0);
    }

    /* find out what port we were assigned and print it out */

    length = sizeof(skaddr);
    if (getsockname(ld, (struct sockaddr *)&skaddr, &length) < 0) {
        printf("Error getsockname\n");
        exit(1);
    }

    /* Go echo every datagram we get */
    echo(ld);
    return 0;
}
