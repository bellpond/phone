#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <netinet/udp.h>
#include <sys/types.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#define N 1000
#define M 1000
#define HOSTNAME_SIZE 100

int s = 0;

void die(char *s) {
    perror(s);
    exit(1);
}

void cut_data(unsigned char *data, int cut_step, int cut) {
  int i,j;
  for(i=0;i<N/cut_step;i++) {
    for(j=0;j<cut;j++) {
      data[cut_step*i+j] = 0;
    }
  }
}

void *send_recv_data() {
  unsigned char buf[M];
  unsigned char data[N];
  while(s == 0) {
    if(read(0, buf, N) == -1) die("read");
  }
  while(1) {
    // 送信
    if(read(0,data,N) == -1) die("read");
    cut_data(data, 50, 30);
    if(send(s,data,N,0) == -1) die("send");
    //受信
    if(recv(s,data,N,0) == -1) die("recv");
    if(write(1,data,N) == -1) die("write");
  }
  return NULL;
}

int server(int port) {
  pthread_t th;
  pthread_create(&th, NULL, send_recv_data, (void *)NULL);


  int ss;
  if((ss = socket(PF_INET, SOCK_STREAM, 0)) == -1) die("socket");

  // bind
  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = INADDR_ANY;
  if(bind(ss,(struct sockaddr *)&addr, sizeof(addr)) == -1) die("bind");

  // listen
  if(listen(ss, 10) == -1) die("listen");

  // accept
  struct sockaddr_in client_addr;
  socklen_t len = sizeof(struct sockaddr_in);
  s = accept(ss, (struct sockaddr *)&client_addr, &len);

  if (s == -1) {
      die("accept");
  }
  pthread_join(th, NULL);
  close(ss);
  close(s);

  return 0;
}

int client(char *hostname, int port) {

    if((s = socket(PF_INET,SOCK_STREAM,0)) == -1) die("socket");

    // connect
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(hostname);
    addr.sin_port = htons(port);
    if(connect(s,(struct sockaddr *)&addr,sizeof(addr)) == -1) die("connect");

    unsigned char data[N];
    unsigned char buf[M];
    while(1) {
      // 受信
      if(recv(s,data,N,0) == -1) die("recv");
      if(write(1,data,N) == -1) die("write");
      // 送信
      if(read(0,data,N) == -1) die("read");
      cut_data(data, 50, 30);
      if(send(s,data,N,0) == -1) die("send");
    }
    close(s);
  return 0;
}

int main(int argc, char *argv[]) {
    int port;
    char hostname[HOSTNAME_SIZE];

    switch (argc) {
      case 2:
        port = atoi(argv[1]);
        server(port);
        break;
      case 3:
        strcpy(hostname, argv[1]);
        port = atoi(argv[2]);
        client(hostname, port);
        break;
      default:
        fprintf(stderr, "Input hostname or port number.");
    }
    return 0;
}
