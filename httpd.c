/*
  服务器创建socket并监听端口连接,当有客户端连接到服务器时，服务器创建一个新线程来处理客户端请求。
  在新线程中如果发现处理的请求需要使用CGI时，则在新线程中fork出一个子进程，用以执行CGI，父进程与
  fork出的子进程之间通过无名管道pipe进行通信。
 */


/* J. David's webserver */
/* This is a simple webserver.
 * Created November 1999 by J. David Blackstone.
 * CSE 4344 (Network concepts), Prof. Zeigler
 * University of Texas at Arlington
 */
/* This program compiles for Sparc Solaris 2.6.
 * To compile for Linux:
 *  1) Comment out the #include <pthread.h> line.
 *  2) Comment out the line that defines the variable newthread.
 *  3) Comment out the two lines that run pthread_create().
 *  4) Uncomment the line that runs accept_request().
 *  5) Remove -lsocket from the Makefile.
 */
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctype.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>
#include <sys/wait.h>
#include <stdlib.h>

#define ISspace(x) isspace((int)(x))

#define SERVER_STRING "Server: jdbhttpd/0.1.0\r\n"

void *accept_request(void *pclient);
void bad_request(int);
void cat(int, FILE *);
void cannot_execute(int);
void error_die(const char *);
void execute_cgi(int, const char *, const char *, const char *);
int get_line(int, char *, int);
void headers(int, const char *);
void not_found(int);
void serve_file(int, const char *);
int startup(u_short *);
void unimplemented(int);

/**********************************************************************/
/* 当服务器的accept()在指定端口接收到客户端的请求时,从而执行这个函数
 * 参数: 与客户端建立连接的套接字 */
/**********************************************************************/
void *accept_request(void *pclient)
{
	int client = *(int *)pclient;
    char buf[1024];
    int numchars;
    char method[255];
    char url[255];
    char path[512];
    size_t i, j;
    struct stat st;
    int cgi = 0;      /* becomes true if server decides this is a CGI program */
    char *query_string = NULL;

    numchars = get_line(client, buf, sizeof(buf));//读取一行
    i = 0;
    j = 0;//作为解析读到buf中的请求数据的游标
    /*把客户端的请求方法存到 method 数组*/ 
    while (!ISspace(buf[j]) && (i < sizeof(method) - 1)) {
        method[i] = buf[j];
        i++;
        j++;
    }
    method[i] = '\0';

	/*如果既不是 GET请求 又不是 POST请求　*/ 
    if (strcasecmp(method, "GET") && strcasecmp(method, "POST")) {
        unimplemented(client);//通知客户端它所请求的web方法还没有实现
        return NULL;
    }

	/* POST请求的时候开启 cgi */	
    if (strcasecmp(method, "POST") == 0)
        cgi = 1;

	//读取URL
    i = 0;
    while (ISspace(buf[j]) && (j < sizeof(buf)))
        j++;
    while (!ISspace(buf[j]) && (i < sizeof(url) - 1) && (j < sizeof(buf))) {
        url[i] = buf[j];
        i++;
        j++;
    }
    url[i] = '\0';

	/*处理 GET 方法*/  
    if (strcasecmp(method, "GET") == 0) {
        query_string = url;
        while ((*query_string != '?') && (*query_string != '\0'))
            query_string++;
		/* GET 方法特点，? 后面为参数*/  
        if (*query_string == '?') {
            cgi = 1; /*开启 cgi */  
            *query_string = '\0';
            query_string++;
        }
    }

	/*格式化 url 到 path 数组，html 文件都在 htdocs 中*/  
    sprintf(path, "htdocs%s", url);
	/*默认情况为 index.html */ 
    if (path[strlen(path) - 1] == '/')
        strcat(path, "index.html");
	/*根据路径找到对应文件。stat()获取文件状态 */ 
    if (stat(path, &st) == -1) {//查找失败
        while ((numchars > 0) && strcmp("\n", buf)) /*读取并丢弃 headers 信息*/  
            numchars = get_line(client, buf, sizeof(buf));
        not_found(client);
    } else {
        if ((st.st_mode & S_IFMT) == S_IFDIR)//请求的文件是目录
            strcat(path, "/index.html");
		//权限判断---由于这里是根据权限来判断是否执行cgi，所以要把不用cgi执行的文件(如html文件)设置为不可执行权限
        if ((st.st_mode & S_IXUSR) || (st.st_mode & S_IXGRP) || (st.st_mode & S_IXOTH))
            cgi = 1;
		/*不是 cgi,直接把服务器文件返回，否则执行 cgi */  
        if (!cgi)
            serve_file(client, path);
        else
            execute_cgi(client, path, method, query_string);
    }

    close(client);
	return NULL;
}

/**********************************************************************/
/* Inform the client that a request it has made has a problem.
 * Parameters: client socket */
/**********************************************************************/
void bad_request(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 400 BAD REQUEST\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "Content-type: text/html\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "<P>Your browser sent a bad request, ");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "such as a POST without a Content-Length.\r\n");
    send(client, buf, sizeof(buf), 0);
}

/**********************************************************************/
/* 通过socket发送一个文件的全部内容.
 * Parameters: client:与客户端建立连接的套接字描述符
 *             resource:文件指针 */
/**********************************************************************/
void cat(int client, FILE *resource)
{
    char buf[1024];

    fgets(buf, sizeof(buf), resource);
    while (!feof(resource)) {
        send(client, buf, strlen(buf), 0);
        fgets(buf, sizeof(buf), resource);
    }
}

/**********************************************************************/
/* 通知客户端,一个CGI脚本不能被执行。
 * Parameter: the client socket descriptor. */
/**********************************************************************/
void cannot_execute(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 500 Internal Server Error\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<P>Error prohibited CGI execution.\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Print out an error message with perror() (for system errors; based
 * on value of errno, which indicates system call errors) and exit the
 * program indicating an error. */
/**********************************************************************/
void error_die(const char *sc)
{
    perror(sc);
    exit(1);
}

/**********************************************************************/
/* 执行一个CGI脚本.  需要设置环境变量。
 * Parameters: client: 与客户端建立连接的套接字描述符
 *             path: CGI 脚本路径
 *　　　　　　 method:请求的方法类型
 *             query_string:请求的内容
 */
/**********************************************************************/
void execute_cgi(int client, const char *path,
                 const char *method, const char *query_string)
{
    char buf[1024];
    int cgi_output[2];
    int cgi_input[2];
    pid_t pid;
    int status;
    int i;
    char c;
    int numchars = 1;
    int content_length = -1;

    buf[0] = 'A';
    buf[1] = '\0';
    if (strcasecmp(method, "GET") == 0)//GET请求
        while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */
            numchars = get_line(client, buf, sizeof(buf));
    else {  /* POST请求 */
		/* 从 POST 的 HTTP 请求中找出 content_length */  
        numchars = get_line(client, buf, sizeof(buf));
        while ((numchars > 0) && strcmp("\n", buf)) {
            buf[15] = '\0';
            if (strcasecmp(buf, "Content-Length:") == 0)
                content_length = atoi(&(buf[16]));
            numchars = get_line(client, buf, sizeof(buf));
        }
		/*没有找到 content_length */  
        if (content_length == -1) {
            bad_request(client);/*错误请求*/  
            return;
        }
    }

	/* 正确，HTTP 状态码 200 */  
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);

	 /* 建立管道*/ 
    if (pipe(cgi_output) < 0) { 
		cannot_execute(client);/*错误处理*/  
        return;
    }
    if (pipe(cgi_input) < 0) {
		cannot_execute(client);/*错误处理*/ 
        return;
    }

	//创建子进程
    if ( (pid = fork()) < 0 ) {
        cannot_execute(client);/*错误处理*/ 
        return;
    }
    if (pid == 0) { /* 子进程: 执行CGI script */
        char meth_env[255];
        char query_env[255];
        char length_env[255];

        dup2(cgi_output[1], 1);//把 STDOUT 重定向到 cgi_output管道的写入端
        dup2(cgi_input[0], 0); //把 STDIN 重定向到 cgi_input管道的读取端
        close(cgi_output[0]);/* 关闭 cgi_input 的写入端 和 cgi_output 的读取端 */  
        close(cgi_input[1]);
		/*设置 request_method 的环境变量*/
        sprintf(meth_env, "REQUEST_METHOD=%s", method);
        putenv(meth_env);
        if (strcasecmp(method, "GET") == 0) {
			/*设置 query_string 的环境变量*/  
            sprintf(query_env, "QUERY_STRING=%s", query_string);
            putenv(query_env);
        } else { /* POST */
            /*设置 content_length 的环境变量*/
            sprintf(length_env, "CONTENT_LENGTH=%d", content_length);
            putenv(length_env);
        }
		
        execl(path, path, NULL);/*用 execl 运行 cgi 程序*/ 
        exit(0);
    } else {    /* 父进程 */
        /* 关闭 cgi_input 的读取端 和 cgi_output 的写入端 */ 
        close(cgi_output[1]);
        close(cgi_input[0]);
        if (strcasecmp(method, "POST") == 0)
			/*接收 POST 过来的数据------这里可以改进*/  
            for (i = 0; i < content_length; i++) {
				/*把 POST 数据写入 cgi_input，现在重定向到 STDIN */
                recv(client, &c, 1, 0);
                write(cgi_input[1], &c, 1);
            }
		/*读取 cgi_output 的管道输出到客户端，该管道输入是 STDOUT */
        while (read(cgi_output[0], &c, 1) > 0)
            send(client, &c, 1, 0);

		/*关闭管道*/ 
        close(cgi_output[0]);
        close(cgi_input[1]);
		/*等待子进程*/ 
        waitpid(pid, &status, 0);
    }
}

/**********************************************************************/
/* 从套接字读取一行,无论是否以换行符,回车,或者CRLF组合结束。null终止字符串读取。
   如果没有在读满缓冲区之前找到换行指示器,将以一个NULL终止字符串.
   如果读到以上三个行结束符,无论是以'\r','\n',还是'\r\n'结尾均转化为以'\n'再加'\0'字符结束。
 * 参数: socket :套接字描述符
 *       buf:    用来保存数据
 *       size:   指定buffer的大小(读取的个数最多为size-1,最后以null结束)
 * 返回: 存储的字节数 (不包括 null) */
/**********************************************************************/
int get_line(int sock, char *buf, int size)
{
    int i = 0;
    char c = '\0';
    int n;

    while ((i < size - 1) && (c != '\n')) {
        n = recv(sock, &c, 1, 0);//从sock中一次读一个字符
        /* DEBUG printf("%02X\n", c); */
        if (n > 0) {
            if (c == '\r') {
				/*以MSG_PEEK方式recv(),仅把tcp buffer中的数据读取到buf中,
				  并不把已读取的数据从tcp buffer中移除,再次调用recv仍然可以读到刚才读到的数据。*/
                n = recv(sock, &c, 1, MSG_PEEK); 
                /* DEBUG printf("%02X\n", c); */
                if ((n > 0) && (c == '\n'))
                    recv(sock, &c, 1, 0);
                else
                    c = '\n';
            }
            buf[i] = c;
            i++;
        } else
            c = '\n';
    }
    buf[i] = '\0';

    return(i);
}

/**********************************************************************/
/* 发送HTTP头信息. */
/* Parameters: client:套接字
 *             filename:文件名 */
/**********************************************************************/
void headers(int client, const char *filename)
{
    char buf[1024];
    (void)filename;  /* could use filename to determine file type */

    strcpy(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Give a client a 404 not found status message. 
　 给客户返回一个错误404 状态消息，表示请求的内容不存在。*/
/**********************************************************************/
void not_found(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 404 NOT FOUND\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><TITLE>Not Found</TITLE>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>The server could not fulfill\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "your request because the resource specified\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "is unavailable or nonexistent.\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* 发送常规文件到客户机.  使用标题,如果他们发生错误则报告。
 * Parameters: client:套接字描述符
 　　　　　　　filename:要发送的文件
 */
/**********************************************************************/
void serve_file(int client, const char *filename)
{
    FILE *resource = NULL;
    int numchars = 1;
    char buf[1024];

    buf[0] = 'A';
    buf[1] = '\0';
    while ((numchars > 0) && strcmp("\n", buf))  /*读取并丢弃 headers 信息*/  
        numchars = get_line(client, buf, sizeof(buf));

    resource = fopen(filename, "r");
    if (resource == NULL)
        not_found(client);
    else {
        headers(client, filename);
        cat(client, resource);
    }
    fclose(resource);
}

/**********************************************************************/
/*这个函数在指定的端口上开始监听网络连接。
 *参数: port: 如果*port=0 表示动态的分配一个端口,并将分配的端口号写入*port中。 
 *            *port=其它:　建立连接的端口号
 * 返回: 监听套接字 */
/**********************************************************************/
int startup(u_short *port)
{
    int httpd = 0;
    struct sockaddr_in name;

    httpd = socket(PF_INET, SOCK_STREAM, 0);
    if (httpd == -1)
        error_die("socket");
    memset(&name, 0, sizeof(name));
    name.sin_family = AF_INET;
    name.sin_port = htons(*port);
    name.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(httpd, (struct sockaddr *)&name, sizeof(name)) < 0)
        error_die("bind");
    if (*port == 0) { /* 如果动态分配端口 */
        socklen_t namelen = sizeof(name);
        if (getsockname(httpd, (struct sockaddr *)&name, &namelen) == -1)
            error_die("getsockname");
        *port = ntohs(name.sin_port);
    }
    if (listen(httpd, 5) < 0)
        error_die("listen");
    return(httpd);
}

/**********************************************************************/
/* 通知客户端所请求的web方法还没有实现
 * Parameter: the client socket */
/**********************************************************************/
void unimplemented(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 501 Method Not Implemented\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><HEAD><TITLE>Method Not Implemented\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</TITLE></HEAD>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>HTTP request method not supported.\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}

/*************************main*********************************************/

int main(void)
{
    int server_sock = -1;
    u_short port = 0;
    int client_sock = -1;
    struct sockaddr_in client_name;
    socklen_t client_name_len = sizeof(client_name);
    pthread_t newthread;

    server_sock = startup(&port);//创建服务端socket，绑定、监听
    printf("httpd running on port %d\n", port);

    while (1) {
		//等待连接
        client_sock = accept(server_sock, (struct sockaddr *)&client_name, &client_name_len);
        if (client_sock == -1)
            error_die("accept");

        /* 创建线程处理请求。accept_request(client_sock)函数用于处理请求; */
        if (pthread_create(&newthread , NULL, accept_request,  (void*)&client_sock) != 0)
            perror("pthread_create");
    }

    close(server_sock);

    return(0);
}



