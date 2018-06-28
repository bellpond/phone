#include <math.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <pthread.h>
#include <complex.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>

#define N 1024
#define M 10
#define HOSTNAME_SIZE 100
#define CUT_STEP 50
#define CUT 30

typedef unsigned char sample_t;

char order = 'n';

void die(char *s) {
    perror(s);
    exit(1);
}




/* fd から 必ず n バイト読み, bufへ書く.
   n バイト未満でEOFに達したら, 残りは0で埋める.
   fd から読み出されたバイト数を返す */
ssize_t read_n(int fd, ssize_t n, void * buf) {
  ssize_t re = 0;
  while (re < n) {
    ssize_t r = read(fd, buf + re, n - re);
    if (r == -1) die("read");
    if (r == 0) break;
    re += r;
  }
  memset(buf + re, 0, n - re);
  return re;
}

/* fdへ, bufからnバイト書く */
ssize_t write_n(int fd, ssize_t n, void * buf) {
  ssize_t wr = 0;
  while (wr < n) {
    ssize_t w = write(fd, buf + wr, n - wr);
    if (w == -1) die("write");
    wr += w;
  }
  return wr;
}

/* 標本(整数)を複素数へ変換 */
void sample_to_complex(sample_t * s,
		       complex double * X,
		       long n) {
  long i;
  for (i = 0; i < n; i++) X[i] = s[i];
}

/* 複素数を標本(整数)へ変換. 虚数部分は無視 */
void complex_to_sample(complex double * X,
		       sample_t * s,
		       long n) {
  long i;
  for (i = 0; i < n; i++) {
    s[i] = creal(X[i]);
  }
}

/* 高速(逆)フーリエ変換;
   w は1のn乗根.
   フーリエ変換の場合   偏角 -2 pi / n
   逆フーリエ変換の場合 偏角  2 pi / n
   xが入力でyが出力.
   xも破壊される
 */
void fft_r(complex double * x,
	   complex double * y,
	   long n,
	   complex double w) {
  if (n == 1) { y[0] = x[0]; }
  else {
    complex double W = 1.0;
    long i;
    for (i = 0; i < n/2; i++) {
      y[i]     =     (x[i] + x[i+n/2]); /* 偶数行 */
      y[i+n/2] = W * (x[i] - x[i+n/2]); /* 奇数行 */
      W *= w;
    }
    fft_r(y,     x,     n/2, w * w);
    fft_r(y+n/2, x+n/2, n/2, w * w);
    for (i = 0; i < n/2; i++) {
      y[2*i]   = x[i];
      y[2*i+1] = x[i+n/2];
    }
  }
}

void fft(complex double * x,
	 complex double * y,
	 long n) {
  long i;
  double arg = 2.0 * M_PI / n;
  complex double w = cos(arg) - 1.0j * sin(arg);
  fft_r(x, y, n, w);
  for (i = 0; i < n; i++) y[i] /= n;
}

void ifft(complex double * y,
	  complex double * x,
	  long n) {
  double arg = 2.0 * M_PI / n;
  complex double w = cos(arg) + 1.0j * sin(arg);
  fft_r(y, x, n, w);
}

int pow2check(long m) {
  long n = m;
  while (n > 1) {
    if (n % 2) return 0;
    n = n / 2;
  }
  return 1;
}

void print_complex(FILE * wp,
		   complex double * Y, long n) {
  long i;
  for (i = 0; i < n; i++) {
    fprintf(wp, "%ld %f %f %f %f\n",
	    i,
	    creal(Y[i]), cimag(Y[i]),
	    cabs(Y[i]), atan2(cimag(Y[i]), creal(Y[i])));
  }
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

int filter(int fi, int fo, const long n) {
  if (!pow2check(n)) {
    fprintf(stderr, "error : n (%ld) not a power of two\n", n);
    exit(1);
  }
  FILE * wp = fopen("fft.dat", "wb");
  if (wp == NULL) die("fopen");
  sample_t * buf = calloc(sizeof(sample_t), n);
  complex double * X = calloc(sizeof(complex double), n);
  complex double * Y = calloc(sizeof(complex double), n);
  while (1) {
    /* 標準入力からn個標本を読む */
    ssize_t m = read_n(fi, n * sizeof(sample_t), buf);
    if (m == 0) break;
    /* 複素数の配列に変換 */
    sample_to_complex(buf, X, n);
    /* FFT -> Y */
    fft(X, Y, n);

    print_complex(wp, Y, n);
    fprintf(wp, "----------------\n");

    /* IFFT -> Z */
    ifft(Y, X, n);
    /* 標本の配列に変換 */
    complex_to_sample(X, buf, n);
    /* 標準出力へ出力 */
    write_n(fo, m, buf);
  }
  fclose(wp);

  return 0;
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
  if((rec_p = popen("rec -t raw -b 16 -c 1 -e s -r 44100 -", "r")) == NULL) die("popen");
  FILE *play_p;
  if((play_p = popen("play -t raw -b 16 -c 1 -e s -r 44100 -", "w")) == NULL) die("popen");

  unsigned char buf[M];
  unsigned char data[N];

  while(order != 'q') {
    // 破棄
    if(fread(buf,sizeof(unsigned char),M,rec_p) == -1) die("read");

    // 送信
    if(fread(data,sizeof(unsigned char),N,rec_p) == -1) die("fread");

    FILE *buf_fip;
    if((buf_fip = fopen("bufi.dat", "wb")) == NULL) die("fopen");
    if(fwrite(data,sizeof(unsigned char),N,buf_fip) == -1) die("fwrite");
    fclose(buf_fip);

    int buf_fid;
    int buf_fod;
    if((buf_fid = open("bufi.dat", O_RDONLY)) == -1) die("open");
    if((buf_fod = open("bufo.dat", O_WRONLY)) == -1) die("open");

    filter(buf_fid, buf_fod, N);

    close(buf_fid);
    close(buf_fod);

    if(order == 'w') cut_data(data, CUT_STEP, CUT);

    FILE *buf_fop;
    if((buf_fop = fopen("bufo.dat", "wb+")) == NULL) die("fopen");
    if(fread(data,sizeof(unsigned char),N,buf_fop) == -1) die("fread");
    fclose(buf_fop);

    if(send(s,data,N,0) == -1) die("send");

    //受信
    if(recv(s,data,N,0) == -1) die("recv");
    if(fwrite(data,sizeof(unsigned char),N,play_p) == -1) die("fwrite");
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
    if((rec_p = popen("rec -t raw -b 16 -c 1 -e s -r 44100 -", "r")) == NULL) die("popen");
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
