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
#define M 10
#define HOSTNAME_SIZE 100
#define CUT_STEP 50
#define CUT 30

char order = 'n';

void die(char *s) {
    perror(s);
    exit(1);
}

void *wait_command() {
  int c;
  while((c = getchar()) != EOF) {
    switch (c) {
      case 'n':
      order = 'n';
      break;
      case 'q':
      order = 'q';
      break;
      case 'w':
      order = 'w';
      break;
    }
  }
  return NULL;
}

void cut_data(unsigned char *data, int cut_step, int cut) {
  int i,j;
  for(i=0;i<N/cut_step;i++) {
    for(j=0;j<cut;j++) {
      data[cut_step*i+j] = 0;
    }
  }
}

int server(int port) {

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
  int s;
  struct sockaddr_in client_addr;
  socklen_t len = sizeof(struct sockaddr_in);
  s = accept(ss, (struct sockaddr *)&client_addr, &len);
  if (s == -1) die("accept");

  FILE *rec_p;
  if((rec_p = popen("rec --buffer 50 --input-buffer 50 -t raw -b 16 -c 1 -e s -r 44100 -", "r")) == NULL) die("popen");
  FILE *play_p;
  if((play_p = popen("play -t raw -b 16 -c 1 -e s -r 44100 -", "w")) == NULL) die("popen");

  unsigned char buf[M];
  unsigned char data[N];

  while(order != 'q') {
    // 破棄
    if(fread(buf,sizeof(unsigned char),M,rec_p) == -1) die("read");
    // 送信
    if(fread(data,sizeof(unsigned char),N,rec_p) == -1) die("read");
    if(order == 'w') cut_data(data, CUT_STEP, CUT);
    if(send(s,data,N,0) == -1) die("send");
    //受信
    if(recv(s,data,N,0) == -1) die("recv");
    if(fwrite(data,sizeof(unsigned char),N,play_p) == -1) die("write");
  }

  pclose(rec_p);
  pclose(play_p);
  close(ss);
  close(s);

  return 0;
}

int client(char *hostname, int port) {
    pthread_t th;
    pthread_create(&th, NULL, wait_command, (void *)NULL);

    int s;
    if((s = socket(PF_INET,SOCK_STREAM,0)) == -1) die("socket");

    // connect
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(hostname);
    addr.sin_port = htons(port);
    if(connect(s,(struct sockaddr *)&addr,sizeof(addr)) == -1) die("connect");

    FILE *rec_p;
    if((rec_p = popen("rec --buffer 25 --input-buffer 25 -t raw -b 16 -c 1 -e s -r 44100 -", "r")) == NULL) die("popen");
    FILE *play_p;
    if((play_p = popen("play -t raw -b 16 -c 1 -e s -r 44100 -", "w")) == NULL) die("popen");

    unsigned char buf[M];
    unsigned char data[N];

    while(order != 'q') {
      // 破棄
      if(fread(buf,sizeof(unsigned char),M,rec_p) == -1) die("read");
      // 送信
      if(fread(data,sizeof(unsigned char),N,rec_p) == -1) die("read");
      if(order == 'w') cut_data(data, CUT_STEP, CUT);
      if(send(s,data,N,0) == -1) die("send");
      //受信
      if(recv(s,data,N,0) == -1) die("recv");
      if(fwrite(data,sizeof(unsigned char),N,play_p) == -1) die("write");
    }
    pclose(rec_p);
    pclose(play_p);
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
