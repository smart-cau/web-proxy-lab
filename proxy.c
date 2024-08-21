#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";
static const char *proxy_http_version = "HTTP/1.0";

/* Proxy-lab
  * 1. Sequential Proxy
    * client가 proxy에 보낸 HTTP/1.1 요청을 서버에 HTTP/1.0으로 바꿔서 보냄
    * 이 때 client request header에 Connection: close, Proxy-connection: close를 추가하여 보낸다
    * 이렇게 server에 보내서 받은 응답을 바로 client에 forward 해준다
*/
/* Function Prototypes */
void transaction(int connfd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *url, char *uri, char *host, char *port);
void request(int p_clientfd, char *method, char *uri, char *host);
void response(int p_connfd, int p_clientfd);

void *task_thread(void *vargp);


int main(int argc, char **argv) {
  int listenfd, *p_connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  pthread_t tid;

  /* Check command line args */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]);

  while (1) {
    clientlen = sizeof(clientaddr);
    p_connfd = Malloc(sizeof(int));
    *p_connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    /*
      * 새로운 클라이언트 연결이 수락될 때마다 새 쓰레드를 생성
      * task_thread 함수 == 새 쓰레드의 시작점(entry point)
      * p_connfd(연결 파일 디스크립터의 포인터)가 새 쓰레드에 인자로 전달
    */
    Pthread_create(&tid, NULL, task_thread, p_connfd);
  }
}

void transaction(int p_connfd) {
  int p_clientfd;
  char buf[MAXLINE],  host[MAXLINE], port[MAXLINE], method[MAXLINE], url[MAXLINE], version[MAXLINE];
  char uri[MAXLINE];
  rio_t rio;
  
  /* Read request line and headers from Client */
  Rio_readinitb(&rio, p_connfd);            // rio 버퍼와 fd(proxy의 connfd)를 연결시켜준다. 
  Rio_readlineb(&rio, buf, MAXLINE);  // 그리고 rio(==proxy의 connfd)에 있는 한 줄(응답 라인)을 모두 buf로 옮긴다. 
  printf("Request headers to proxy:\n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, url, version); // buf에서 문자열 3개를 읽어와 각각 method, uri, version이라는 문자열에 저장 

  /* Parse URI from GET request */
  parse_uri(url, uri, host, port);

  p_clientfd = Open_clientfd(host, port);
  request(p_clientfd, method, uri, host);
  response(p_connfd, p_clientfd);        
  Close(p_clientfd);
}

/* Reads and ignores request headers */
void read_requesthdrs(rio_t *rp) {
  char buf[MAXLINE];
  // request header에 있는 어떤 정보도 사용하지 않고 line 별로 읽고 넘김
  Rio_readlineb(rp, buf, MAXLINE);
  while (strcmp(buf, "\r\n")) {
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
  }
  return;
}

/* parse_uri: Parse URI from (Client로 부터 받은) GET request, Proxy에서 Server로의 GET request를 하기 위해 필요 */
int parse_uri(char *uri, char *uri_ptos, char *host, char *port){ 
  char *ptr;

  if (!(ptr = strstr(uri, "://")))
    return -1;
  ptr += 3;                       
  strcpy(host, ptr);              // host = www.google.com:80/index.html

  if((ptr = strchr(host, ':'))){  // strchr(): 문자 하나만 찾는 함수 (''작은따옴표사용)
    *ptr = '\0';                  // host = www.google.com
    ptr += 1;
    strcpy(port, ptr);            // port = 80/index.html
  }
  else{
    if((ptr = strchr(host, '/'))){
      *ptr = '\0';
      ptr += 1;
    }
    strcpy(port, "80"); 
  }

  if ((ptr = strchr(port, '/'))){ // port = 80/index.html
    *ptr = '\0';                  // port = 80
    ptr += 1;     
    strcpy(uri_ptos, "/");        // uri_ptos = /
    strcat(uri_ptos, ptr);        // uri_ptos = /index.html
  }  
  else strcpy(uri_ptos, "/");

  return 0; // function int return => for valid check
}

/* request proxy to server */
void request(int p_clientfd, char *method, char *uri, char *host) {
  char buf[MAXLINE];
  printf("Request headers to server: \n");
  printf("%s %s %s\n", method, uri, proxy_http_version);

  /* read request headers */
  sprintf(buf, "GET %s %s\r\n", uri, proxy_http_version); // GET /index.html HTTP/1.0
  sprintf(buf, "%sHost: %s\r\n", buf, host); // Host: www.google.com
  sprintf(buf, "%s%s", buf, user_agent_hdr); // User-Agent: ...
  sprintf(buf, "%sConnections: close\r\n", buf); // Connections: close
  sprintf(buf, "%sProxy-Connection: close\r\n\r\n", buf); // Proxy-Connection: close

  /* write buf to clientfd */
  Rio_writen(p_clientfd, buf, (size_t)strlen(buf));
}

/* response server -> proxy -> client */
void response(int p_connfd, int p_clientfd) {
  char buf[MAX_CACHE_SIZE];
  ssize_t n;
  rio_t rio;

  Rio_readinitb(&rio, p_clientfd); // get response from server and save it in rio buffer
  n = Rio_readnb(&rio, buf, MAX_CACHE_SIZE); // write response in rio buffer into buf
  Rio_writen(p_connfd, buf, n); // send response to client
}

/* client 각각의 연결을 개별 thread로 처리 */
void *task_thread(void *vargp) {
  int p_connfd = *((int *)vargp); // vargp에는 thread 생성 시 받은 p_connfd의 pointer가 있음. p_connfd가 가르키는 값(fd)만 복사해서 저장
  Free(vargp); // p_connfd 생성 시 할당 받았던 메모리 반환

  Pthread_detach(pthread_self()); // 생성된 thread를 parent thread로부터 detach 시킴. 이로써 thread 종료 시 관련 자원을 자동으로 반환하게 함

  transaction(p_connfd); // transaction 처리
  Close(p_connfd); // 열었던 p_connfd Close

  return NULL;
}