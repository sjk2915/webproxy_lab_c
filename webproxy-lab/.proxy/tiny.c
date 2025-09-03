/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"

void doit(int fd);
int read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize, char *method);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs, char *method, int content_length, char *post_data);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);
void sigchld_handler(int sig);

int main(int argc, char **argv)
{
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  /* Check command line args */
  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]);
  Signal(SIGCHLD, sigchld_handler);
  // EPIPE 에러 무시
  Signal(SIGPIPE, SIG_IGN);

  while (1)
  {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); // line:netp:tiny:accept
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE,
                port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    doit(connfd);  // line:netp:tiny:doit
    Close(connfd); // line:netp:tiny:close
  }
}

void doit(int fd)
{
  int is_static;
  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio;

  Rio_readinitb(&rio, fd);
  Rio_readlineb(&rio, buf, MAXLINE);
  printf("Request Line:\n%s\n", buf);
  sscanf(buf, "%s %s %s", method, uri, version);
  // GET도 아니고 HEAD도 아니고 POST도 아니면
  if (strcasecmp(method, "GET") && strcasecmp(method, "HEAD") && strcasecmp(method, "POST"))
  {
    clienterror(fd, method, "501", "Not implemented",
              "Tiny does not implement this method");
    return;
  }

  int content_length = read_requesthdrs(&rio);

  is_static = parse_uri(uri, filename, cgiargs);
  if (stat(filename, &sbuf) < 0)
  {
    clienterror(fd, filename, "404", "Not found",
                "Tiny couldn't find this file");
    return;
  }

  if (is_static)
  {
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode))
    {
      clienterror(fd, filename, "403", "forbidden",
                  "Tiny couldn't read the file");
      return;
    }
    serve_static(fd, filename, sbuf.st_size, method);
  }
  else
  {
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode))
    {
      clienterror(fd, filename, "403", "forbidden",
                  "Tiny couldn't run the CGI program");
      return;
    }
    char post_data[MAXLINE] = {0};
    if (content_length > 0)
      Rio_readnb(&rio, post_data, content_length);

    serve_dynamic(fd, filename, cgiargs, method, content_length, post_data);
  }
}

void clienterror(int fd, char *cause, char *errnum,
                 char *shortmsg, char *longmsg)
{
  char buf[MAXLINE], body[MAXBUF];

  // HTTP 응답 바디 빌드
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  // HTTP 응답 출력
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}

int read_requesthdrs(rio_t *rp)
{
  char buf[MAXLINE];
  int content_length = 0;

  printf("Request headers:\n");
  while(Rio_readlineb(rp, buf, MAXLINE) > 0 && strcmp(buf, "\r\n"))
  {
    if (strncasecmp(buf, "Content-Length:", 15) == 0)
      sscanf(buf, "Content-Length: %d", &content_length);
    printf("%s", buf);
  }
  printf("\n");
  return content_length;
}

int parse_uri(char *uri, char *filename, char *cgiargs)
{
  char *ptr;

  // static content
  if (!strstr(uri, "cgi-bin"))
  {
    strcpy(cgiargs, "");
    strcpy(filename, ".");
    strcat(filename, uri);
    if (uri[strlen(uri)-1] == '/')
      strcat(filename, "home.html");
    return 1;
  }
  // dynamic content
  else
  {
    ptr = index(uri, '?');
    if (ptr)
    {
      strcpy(cgiargs, ptr+1);
      *ptr = '\0';
    }
    else
      strcpy(cgiargs, "");
    strcpy(filename, ".");
    strcat(filename, uri);
    return 0;
  }
}

void get_filetype(char *filename, char *filetype)
{
  if (strstr(filename, ".html"))
    strcpy(filetype, "text/html");
  else if (strstr(filename, ".gif"))
    strcpy(filetype, "image/gif");
  else if (strstr(filename, ".png"))
    strcpy(filetype, "image/png");
  else if (strstr(filename, ".jpg"))
    strcpy(filetype, "image/jpeg");
  else if (strstr(filename, ".mpg"))
    strcpy(filetype, "video/mpeg");
  else
    strcpy(filetype, "text/plain");
}

void serve_static(int fd, char *filename, int filesize, char *method)
{
  int srcfd;
  char *srcp, filetype[MAXLINE], buf[MAXBUF];

  // 응답 헤더
  get_filetype(filename, filetype);
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
  sprintf(buf, "%sConnection: close\r\n", buf);
  sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
  sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
  // 출력
  Rio_writen(fd, buf, strlen(buf));
  printf("Responseheaders:\n");
  printf("%s", buf);

  if (strcasecmp(method, "GET") == 0)
  {
    // 응답 바디
    srcfd = Open(filename, O_RDONLY, 0);
    srcp = (char *)malloc(filesize);
    Rio_readn(srcfd, srcp, filesize);
    Close(srcfd);
    // 출력
    Rio_writen(fd, srcp, filesize);
    free(srcp);
  }
}

void sigchld_handler(int sig)
{
  // 좀비 프로세스 수거
  while (waitpid(-1, NULL, WNOHANG) > 0) {}
  return; 
}

void serve_dynamic(int fd, char *filename, char *cgiargs,
                   char *method, int content_length, char *post_data)
{
  char buf[MAXLINE], *emptylist[] = { NULL };
  char content_length_str[20];

  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Server: Tiny Web Server\r\n");
  Rio_writen(fd, buf, strlen(buf));

  if (strcasecmp(method, "GET") == 0)
  {
    // 자식 프로세스
    if (Fork() == 0)
    {
      setenv("REQUEST_METHOD", method, 1);
      setenv("QUERY_STRING", cgiargs, 1);
      Dup2(fd, STDOUT_FILENO);
      Execve(filename, emptylist, environ);
    }
  }

  else if (strcasecmp(method, "POST") == 0)
  {
    int pfd[2];
    if (pipe(pfd) < 0)
    {
        clienterror(fd, "pipe()", "500", "Internal Server Error", "Faileld to create pipe");
        return;
    }

    // 자식 프로세스
    if (Fork() == 0)
    {
      // 쓰기 닫기
      Close(pfd[1]);
      setenv("REQUEST_METHOD", method, 1);
      sprintf(content_length_str, "%d", content_length);
      setenv("CONTENT_LENGTH", content_length_str, 1);
      Dup2(pfd[0], STDIN_FILENO);
      Close(pfd[0]);
      Dup2(fd, STDOUT_FILENO);
      Execve(filename, emptylist, environ);
      exit(0);
    }
    // 부모 프로세스
    else
    {
      // 읽기 닫기
      Close(pfd[0]);
      // 본문 파이프에 쓰기
      Rio_writen(pfd[1], post_data, content_length);
      Close(pfd[1]);
      waitpid(-1, NULL, 0);
    }
  }
}