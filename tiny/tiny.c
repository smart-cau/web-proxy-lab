/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"

void doit(int connfd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int connfd, char *filename, int filesize, char* method);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int connfd, char *filename, char *cgiargs, char* method);
void clienterror(int connfd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);
void skip_handler(int signal) {
  return;
}
int main(int argc, char **argv) {
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  /* Check command line args */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]);
  Signal(SIGPIPE, skip_handler);
  while (1) {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr,
                    &clientlen);  // line:netp:tiny:accept
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,
                0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    doit(connfd);   // line:netp:tiny:doit
    Close(connfd);  // line:netp:tiny:close
  }
}

void doit(int connfd) {
  int is_static;
  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio;

  /* Read request line and headers */
  Rio_readinitb(&rio, connfd);
  Rio_readlineb(&rio, buf, MAXLINE);
  printf("Request headers: \n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version);
  
  if (strcasecmp(method, "GET") && strcasecmp(method, "HEAD")) {
    clienterror(connfd, method, "501", "Not implemented", "Tiny does not implement this method");
    return;
  }

  read_requesthdrs(&rio);

  /* Parse URI from GET request */
  is_static = parse_uri(uri, filename, cgiargs);
  if (stat(filename, &sbuf) < 0) {
    clienterror(connfd, filename, "404", "Not found", "Tiny couldn't find this file");
    return;
  }

  if (is_static) { /* Serve static content */
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
      clienterror(connfd, filename, "403", "Forbidden", "Tiny counldn't read the file");
      return;
    }
    serve_static(connfd, filename, sbuf.st_size, method);
  }
  else { /* Serve dynamic content */
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) {
      clienterror(connfd, filename, "403", "Forbidden", "Tiny counldn't run the CGI program");
      return;
    }
    serve_dynamic(connfd, filename, cgiargs, method);
  }
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

int parse_uri(char *uri, char *filename, char *cgiargs) {
    char *ptr;

    if (!strstr(uri, "cgi-bin")) { /* Static content */
      strcpy(cgiargs, ""); // cgiargs 내용 밀어줌
      strcpy(filename, "."); // filename = "."
      strcat(filename, uri); // filename = "./<static_file>"
      if (uri[strlen(uri)-1] == '/') // uri가 /으로 끝나면
        strcat(filename, "home.html"); // 뒤에 home.html을 붙여줌
      return 1;
    }
    else { /* Dynamic content */
      ptr = index(uri, '?');
      if (ptr) {
        strcpy(cgiargs, ptr + 1);
        *ptr = '\0';
      }
      else  
        strcpy(cgiargs, "");
      strcpy(filename, ".");
      strcat(filename, uri); // uri의 남은 부분을 relative linux filename만드는데 사용
      return 0;
    }
}

void serve_static(int connfd, char *filename, int filesize, char* method) {
  int srcfd;
  char *srcp, filetype[MAXLINE], buf[MAXBUF];

  /* Send response headers to client */
  get_filetype(filename, filetype);
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
  sprintf(buf, "%sConnection: close\r\n", buf);
  sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
  sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
  Rio_writen(connfd, buf, strlen(buf));
  printf("%s",buf);
  if (!strcasecmp(method, "HEAD"))
    return;
  /* Send response body to client */  
  srcfd = Open(filename, O_RDONLY, 0);
  /* use file with memory mapping 
    srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0); // virual memory에 filesize만큼 공간을 할당받아 srcfd가 가르키는 file을 vm의 private read-only area에 할당받음. 해당 vm의 시작 포인터가 srcp
    Close(srcfd);
    Munmap(srcp, filesize);
  */
  
  srcp = (char *)Malloc(filesize);
  Rio_readn(srcfd, srcp, filesize);
  Rio_writen(connfd, srcp, filesize);
  free(srcp);
}
/*
* get_filetype - Derive file type from filename
*/
void get_filetype(char *filename, char *filetype) {
  if (strstr(filename, ".html"))
    strcpy(filetype, "text/html");
  else if (strstr(filename, ".gif"))
    strcpy(filetype, ".gif");
  else if (strstr(filename, ".png"))
    strcpy(filetype, "image/png");
  else if (strstr(filename, ".jpg"))
    strcpy(filetype, "image/jpeg");
  else if (strstr(filename, ".mpg"))
    strcpy(filetype, "video/mpeg");
  else
    strcpy(filetype, "text/plain");
}

void serve_dynamic(int connfd, char *filename, char *cgiargs, char* method) {
  char buf[MAXLINE], *emptylist[] = { NULL };
  /* Return first part of HTTP response */
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  Rio_writen(connfd, buf, strlen(buf));
  sprintf(buf, "Server: Tiny Web Server\r\n");
  Rio_writen(connfd, buf, strlen(buf));

  if (Fork() == 0) { /* Child */
    /* Real server would set all CGI vars here */
    setenv("QUERY_STRING", cgiargs, 1);
    setenv("REQUEST_METHOD", method, 1);
    Dup2(connfd, STDOUT_FILENO); /* Redirect stdout to client*/
    Execve(filename, emptylist, environ); /* Run CGI program */
  }
  Wait(NULL); /* Parent waits for and reaps child */
}

void clienterror(int connfd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
  char buf[MAXLINE], body[MAXBUF];

  /* Build the HTTP response body */
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  /* Print the HTTP response */
  sprintf(buf, "HTTP/1.0 %s %s \r\n", errnum, shortmsg);
  Rio_writen(connfd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(connfd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(connfd, buf, strlen(buf));
  Rio_writen(connfd, body, strlen(body));
}
