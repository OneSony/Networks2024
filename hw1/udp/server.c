#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>     /* defines STDIN_FILENO, system calls,etc */
#include <sys/types.h>  /* system data type definitions */
#include <sys/socket.h> /* socket specific definitions */
#include <netinet/in.h> /* INET constants and stuff */
#include <arpa/inet.h>  /* IP address conversion stuff */
#include <netdb.h>      /* gethostbyname */
#include <string.h>

#define MAXBUF 10*1024

void uppercase(char *p) {
  for ( ; *p; ++p) *p = toupper(*p);
}

void echo(int sd) {
    char bufin[MAXBUF];
    struct sockaddr_in remote;

    /* need to know how big address struct is, len must be set before the
       call to recvfrom!!! */
    socklen_t len = sizeof(remote);
    int order=0;

    /* read a datagram from the socket (put result in bufin) */
    char msg_bufin[51][MAXBUF];
    int msg_index[51];

    while(order<51){
      int n = recvfrom(sd, bufin, MAXBUF, 0, (struct sockaddr *) &remote, &len);

      if (n < 0) {
        perror("Error receiving data");
      } else {
        //uppercase(bufin);
        /* Got something, just send it back */

        n=n<MAXBUF-1?n:MAXBUF-1;
        bufin[n]='\0';

        int recv_index;
        char msg[MAXBUF];

        sscanf(bufin,"%d %s", &recv_index, msg);

        printf("%d %s\n", recv_index, msg);

        memcpy(msg_bufin[recv_index],msg,MAXBUF);
        msg_index[recv_index]=order+1;

        order++;
      }
    }

    printf("recv finished\n");

    for(int i=0;i<51;i++){
      char bufin[MAXBUF];
      int n = snprintf(bufin, MAXBUF, "%d %s", msg_index[i], msg_bufin[i]);
      printf("%s\n",bufin);
      sendto(sd, bufin, n, 0, (struct sockaddr *)&remote, len);
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
  skaddr.sin_addr.s_addr = htonl(INADDR_ANY); //收听所有的ip地址
  skaddr.sin_port = htons(9876);

  if (bind(ld, (struct sockaddr *) &skaddr, sizeof(skaddr)) < 0) {
    printf("Problem binding\n");
    exit(0);
  }

  /* find out what port we were assigned and print it out */

  length = sizeof(skaddr);
  if (getsockname(ld, (struct sockaddr *) &skaddr, &length) < 0) {
    printf("Error getsockname\n");
    exit(1);
  }

  /* Go echo every datagram we get */
  echo(ld);
  return 0;
}
