/*
 * adder.c - a minimal CGI program that adds two numbers together
 */
/* $begin adder */
#include "csapp.h"

int main(void)
{
  char *buf, *p;
  char arg1[MAXLINE], arg2[MAXLINE], content[MAXLINE];
  int n1 = 0, n2 = 0;

  char *method = getenv("REQUEST_METHOD");
  // GET 요청 처리
  if (method && strcasecmp(method, "GET") == 0)
  {
    buf = getenv("QUERY_STRING");

    p = strchr(buf, '&');
    *p = '\0';
    strcpy(arg1, buf);
    strcpy(arg2, p + 1);
    n1 = atoi(strchr(arg1, '=') + 1);
    n2 = atoi(strchr(arg2, '=') + 1);
  }
  // POST 요청 처리
  else if (method && strcasecmp(method, "POST") == 0)
  {
    int len = atoi(getenv("CONTENT_LENGTH"));
    char post_data[MAXLINE];

    if (Rio_readn(STDIN_FILENO, post_data, len) > 0)
    {
      post_data[len] = '\0';

      p = strchr(post_data, '&');
      *p = '\0';
      strcpy(arg1, post_data);
      strcpy(arg2, p + 1);
      n1 = atoi(strchr(arg1, '=') + 1);
      n2 = atoi(strchr(arg2, '=') + 1);
    }
  }

  /* Make the response body */
  sprintf(content, "Welcome to add.com: ");
  sprintf(content + strlen(content), "The Internet addition portal.\r\n<p>");
  sprintf(content + strlen(content), "The answer is: %d + %d = %d\r\n<p>",
          n1, n2, n1 + n2);
  sprintf(content + strlen(content), "Thanks for visiting!\r\n");

  /* Generate the HTTP response */
  printf("Connection: close\r\n");
  printf("Content-length: %d\r\n", (int)strlen(content));
  printf("Content-type: text/html\r\n\r\n");
  printf("%s", content);
  fflush(stdout);

  exit(0);
}
/* $end adder */
