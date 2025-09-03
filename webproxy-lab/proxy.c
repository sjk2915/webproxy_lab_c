#include <stdio.h>
#include "csapp.h"
#include <sys/ipc.h>
#include <sys/shm.h>
#include <semaphore.h>

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

#define CACHE_SHM_KEY 1234
#define SEM_KEY 5678

typedef struct cache_object {
    char uri[MAXLINE];
    char *data;
    size_t data_size;
    struct cache_object *next;
} CacheObject;

typedef struct {
    CacheObject *head;
    size_t total_size;
} Cache;

void *thread(void *vargp);
void doit(int fd);
void read_requesthdrs(rio_t *rp, char *hosthdr, char *other_hdr);
void parse_uri(char *uri, char *path);
void parse_hosthdr(char *hosthdr, char *hostname, char *port);
void request_and_response(int confd, int clientfd, char* uri,
                          char *method, char *path, char *hostname, char *other_hdr);
void sigchld_handler(int sig);
void init_cache();
CacheObject *get_cache(char *uri);
void add_to_cache(char *uri, char *data, size_t data_size);

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

static Cache cache;
static pthread_rwlock_t rwlock;

int main(int argc, char **argv)
{
  int listenfd, *connfdp;
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  pthread_t tid;

  /* Check command line args */
  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  Signal(SIGCHLD, sigchld_handler);
  init_cache();

  listenfd = Open_listenfd(argv[1]);
  while (1)
  {
    clientlen = sizeof(clientaddr);
    connfdp = Malloc(sizeof(int));
    *connfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    Pthread_create(&tid, NULL, thread, connfdp);
  }
}

void *thread(void *vargp)
{
    int connfd = *((int *)vargp);
    // 스레드가 종료되면 자원을 자동으로 회수
    Pthread_detach(pthread_self());
    // 힙에 할당한 메모리 해제
    Free(vargp);
    doit(connfd);
    Close(connfd);
    return NULL;
}

/*
 * doit - handle one HTTP request/response transaction
 */
void doit(int fd)
{
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char hosthdr[MAXLINE], other_hdr[MAXLINE];
  char hostname[MAXLINE], port[MAXLINE], path[MAXLINE];
  rio_t rio;

  /* Read request line and headers */
  Rio_readinitb(&rio, fd);
  if (!Rio_readlineb(&rio, buf, MAXLINE))
    return;
  printf("Request Line:\n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version);
  parse_uri(uri, path);
  read_requesthdrs(&rio, hosthdr, other_hdr);

  CacheObject *cacheobj = get_cache(uri);
  // 캐시 히트
  if (cacheobj != NULL)
  {
    Rio_writen(fd, cacheobj->data, cacheobj->data_size);
  }
  // 캐시에 없으면 서버와 연락하기
  else
  {
    parse_hosthdr(hosthdr, hostname, port);
    int clientfd = Open_clientfd(hostname, port);
    request_and_response(clientfd, fd, uri,
                         method, path, hostname, other_hdr);
    close(clientfd);
  }
}

/*
 * read_requesthdrs - read HTTP request headers
 */
void read_requesthdrs(rio_t *rp, char *hosthdr, char *other_hdr)
{
  char buf[MAXLINE];
  hosthdr[0] = '\0';
  other_hdr[0] = '\0';

  while (Rio_readlineb(rp, buf, MAXLINE) > 0 && strcmp(buf, "\r\n"))
  {
    if (strncasecmp(buf, "Host:", 5) == 0)
      sscanf(buf, "Host: %s", hosthdr);
    else
      strcat(other_hdr, buf);
  }
}

void parse_hosthdr(char *hosthdr, char *hostname, char *port)
{
  char *host_end = strchr(hosthdr, ':');
  if (host_end)
  {
      // 콜론이 있으면 호스트와 포트를 분리합니다.
      *host_end = '\0';
      strcpy(hostname, hosthdr);
      strcpy(port, host_end + 1);
  }
  else
  {
      // 콜론이 없으면 기본 포트 80을 사용합니다.
      strcpy(hostname, hosthdr);
      strcpy(port, "80");
  }
}

void parse_uri(char *uri, char *path)
{
  char *hostbegin, *pathbegin;
  char buf[MAXLINE];

  strcpy(buf, uri);

  hostbegin = strstr(buf, "//");
  hostbegin = (hostbegin != NULL) ? hostbegin + 2 : buf; 

  pathbegin = strchr(hostbegin, '/');
  if (pathbegin != NULL)
  {
    strcpy(path, pathbegin);
    *pathbegin = '\0';
  }
  else
  {
    strcpy(path, "/");
  }
}

void request_and_response(int clientfd, int confd, char *uri,
                          char *method, char *path, char *hostname, char *other_hdr)
{
  char req[MAXLINE], response_buf[MAXLINE], cache_buf[MAX_OBJECT_SIZE];
  rio_t client_rio;
  Rio_readinitb(&client_rio, clientfd);
  size_t total_size = 0;
  ssize_t n;

  sprintf(req,
    "%s %s HTTP/1.0\r\n"
    "Host: %s\r\n"
    "%s"
    "Connection: close\r\n"
    "Proxy-Connection: close\r\n"
    "%s"
    "\r\n",
    method, path,
    hostname,
    user_agent_hdr,
    other_hdr
  );
  Rio_writen(clientfd, req, strlen(req));

  // 응답 읽고 써주기
  while ((n = Rio_readnb(&client_rio, response_buf, MAXLINE)) > 0)
  {
    // 응답 전송
    Rio_writen(confd, response_buf, n);
    // 캐시에 데이터 복사
    if (total_size + n <= MAX_OBJECT_SIZE)
    {
      memcpy(cache_buf + total_size, response_buf, n);
      total_size += n;
    }
  }
  add_to_cache(uri, cache_buf, total_size);
}

void sigchld_handler(int sig)
{
  // 좀비 프로세스 수거
  while (waitpid(-1, NULL, WNOHANG) > 0) {}
  return;
}

void init_cache()
{
    cache.head = NULL;
    cache.total_size = 0;
    // 락 초기화
    pthread_rwlock_init(&rwlock, NULL);
}

// uri로 연결리스트에서 캐시 검색
CacheObject *get_cache(char *uri)
{
  // 락 획득
  pthread_rwlock_wrlock(&rwlock);

  CacheObject *cur = cache.head;
  CacheObject *pre = NULL;
  while (cur != NULL)
  {
      if (strcmp(cur->uri, uri) == 0)
      {
        // 최근 사용한거 앞으로 옮기기
        if (pre != NULL)
        {
          pre->next = cur->next;
          cur->next = cache.head;
          cache.head = cur;
        }
        // 캐시 히트: 찾았으니 락 해제
        pthread_rwlock_unlock(&rwlock);
        return cur;
      }
      pre = cur;
      cur = cur->next;
  }
  // 캐시 미스: 못 찾았으니 락 해제
  pthread_rwlock_unlock(&rwlock);
  return NULL;
}

// 뮤텍스 락을 얻고, 데이터를 캐시에 추가합니다.
void add_to_cache(char *uri, char *data, size_t data_size)
{
  // 너무 큰 객체는 캐싱하지 않음
  if (data_size > MAX_OBJECT_SIZE) return;

  pthread_rwlock_wrlock(&rwlock);

  CacheObject *new_node = (CacheObject *)malloc(sizeof(CacheObject) + data_size);
  new_node->data = (char *)new_node + sizeof(CacheObject);

  // 데이터 복사 및 연결 리스트에 추가
  strcpy(new_node->uri, uri);
  memcpy(new_node->data, data, data_size);
  new_node->data_size = data_size;
  new_node->next = cache.head;
  cache.head = new_node;
  cache.total_size += data_size;

  // 캐시 크기 초과 시 가장 오래된 항목 제거 (LRU 구현)
  while (cache.total_size > MAX_CACHE_SIZE)
  {
    if (cache.head == NULL) return;

    CacheObject *tail = cache.head;
    CacheObject *prev = NULL;
    while (tail->next != NULL)
    {
        prev = tail;
        tail = tail->next;
    }

    if (prev == NULL)
      cache.head = NULL;
    else
      prev->next = NULL;

    cache.total_size -= tail->data_size;
    free(tail);
  }

  pthread_rwlock_unlock(&rwlock);
}