/*
  ����������socket�������˿�����,���пͻ������ӵ�������ʱ������������һ�����߳�������ͻ�������
  �����߳���������ִ����������Ҫʹ��CGIʱ���������߳���fork��һ���ӽ��̣�����ִ��CGI����������
  fork�����ӽ���֮��ͨ�������ܵ�pipe����ͨ�š�
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
/* ����������accept()��ָ���˿ڽ��յ��ͻ��˵�����ʱ,�Ӷ�ִ���������
 * ����: ��ͻ��˽������ӵ��׽��� */
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

    numchars = get_line(client, buf, sizeof(buf));//��ȡһ��
    i = 0;
    j = 0;//��Ϊ��������buf�е��������ݵ��α�
    /*�ѿͻ��˵����󷽷��浽 method ����*/ 
    while (!ISspace(buf[j]) && (i < sizeof(method) - 1)) {
        method[i] = buf[j];
        i++;
        j++;
    }
    method[i] = '\0';

	/*����Ȳ��� GET���� �ֲ��� POST����*/ 
    if (strcasecmp(method, "GET") && strcasecmp(method, "POST")) {
        unimplemented(client);//֪ͨ�ͻ������������web������û��ʵ��
        return NULL;
    }

	/* POST�����ʱ���� cgi */	
    if (strcasecmp(method, "POST") == 0)
        cgi = 1;

	//��ȡURL
    i = 0;
    while (ISspace(buf[j]) && (j < sizeof(buf)))
        j++;
    while (!ISspace(buf[j]) && (i < sizeof(url) - 1) && (j < sizeof(buf))) {
        url[i] = buf[j];
        i++;
        j++;
    }
    url[i] = '\0';

	/*���� GET ����*/  
    if (strcasecmp(method, "GET") == 0) {
        query_string = url;
        while ((*query_string != '?') && (*query_string != '\0'))
            query_string++;
		/* GET �����ص㣬? ����Ϊ����*/  
        if (*query_string == '?') {
            cgi = 1; /*���� cgi */  
            *query_string = '\0';
            query_string++;
        }
    }

	/*��ʽ�� url �� path ���飬html �ļ����� htdocs ��*/  
    sprintf(path, "htdocs%s", url);
	/*Ĭ�����Ϊ index.html */ 
    if (path[strlen(path) - 1] == '/')
        strcat(path, "index.html");
	/*����·���ҵ���Ӧ�ļ���stat()��ȡ�ļ�״̬ */ 
    if (stat(path, &st) == -1) {//����ʧ��
        while ((numchars > 0) && strcmp("\n", buf)) /*��ȡ������ headers ��Ϣ*/  
            numchars = get_line(client, buf, sizeof(buf));
        not_found(client);
    } else {
        if ((st.st_mode & S_IFMT) == S_IFDIR)//������ļ���Ŀ¼
            strcat(path, "/index.html");
		//Ȩ���ж�---���������Ǹ���Ȩ�����ж��Ƿ�ִ��cgi������Ҫ�Ѳ���cgiִ�е��ļ�(��html�ļ�)����Ϊ����ִ��Ȩ��
        if ((st.st_mode & S_IXUSR) || (st.st_mode & S_IXGRP) || (st.st_mode & S_IXOTH))
            cgi = 1;
		/*���� cgi,ֱ�Ӱѷ������ļ����أ�����ִ�� cgi */  
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
/* ͨ��socket����һ���ļ���ȫ������.
 * Parameters: client:��ͻ��˽������ӵ��׽���������
 *             resource:�ļ�ָ�� */
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
/* ֪ͨ�ͻ���,һ��CGI�ű����ܱ�ִ�С�
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
/* ִ��һ��CGI�ű�.  ��Ҫ���û���������
 * Parameters: client: ��ͻ��˽������ӵ��׽���������
 *             path: CGI �ű�·��
 *������������ method:����ķ�������
 *             query_string:���������
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
    if (strcasecmp(method, "GET") == 0)//GET����
        while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */
            numchars = get_line(client, buf, sizeof(buf));
    else {  /* POST���� */
		/* �� POST �� HTTP �������ҳ� content_length */  
        numchars = get_line(client, buf, sizeof(buf));
        while ((numchars > 0) && strcmp("\n", buf)) {
            buf[15] = '\0';
            if (strcasecmp(buf, "Content-Length:") == 0)
                content_length = atoi(&(buf[16]));
            numchars = get_line(client, buf, sizeof(buf));
        }
		/*û���ҵ� content_length */  
        if (content_length == -1) {
            bad_request(client);/*��������*/  
            return;
        }
    }

	/* ��ȷ��HTTP ״̬�� 200 */  
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);

	 /* �����ܵ�*/ 
    if (pipe(cgi_output) < 0) { 
		cannot_execute(client);/*������*/  
        return;
    }
    if (pipe(cgi_input) < 0) {
		cannot_execute(client);/*������*/ 
        return;
    }

	//�����ӽ���
    if ( (pid = fork()) < 0 ) {
        cannot_execute(client);/*������*/ 
        return;
    }
    if (pid == 0) { /* �ӽ���: ִ��CGI script */
        char meth_env[255];
        char query_env[255];
        char length_env[255];

        dup2(cgi_output[1], 1);//�� STDOUT �ض��� cgi_output�ܵ���д���
        dup2(cgi_input[0], 0); //�� STDIN �ض��� cgi_input�ܵ��Ķ�ȡ��
        close(cgi_output[0]);/* �ر� cgi_input ��д��� �� cgi_output �Ķ�ȡ�� */  
        close(cgi_input[1]);
		/*���� request_method �Ļ�������*/
        sprintf(meth_env, "REQUEST_METHOD=%s", method);
        putenv(meth_env);
        if (strcasecmp(method, "GET") == 0) {
			/*���� query_string �Ļ�������*/  
            sprintf(query_env, "QUERY_STRING=%s", query_string);
            putenv(query_env);
        } else { /* POST */
            /*���� content_length �Ļ�������*/
            sprintf(length_env, "CONTENT_LENGTH=%d", content_length);
            putenv(length_env);
        }
		
        execl(path, path, NULL);/*�� execl ���� cgi ����*/ 
        exit(0);
    } else {    /* ������ */
        /* �ر� cgi_input �Ķ�ȡ�� �� cgi_output ��д��� */ 
        close(cgi_output[1]);
        close(cgi_input[0]);
        if (strcasecmp(method, "POST") == 0)
			/*���� POST ����������------������ԸĽ�*/  
            for (i = 0; i < content_length; i++) {
				/*�� POST ����д�� cgi_input�������ض��� STDIN */
                recv(client, &c, 1, 0);
                write(cgi_input[1], &c, 1);
            }
		/*��ȡ cgi_output �Ĺܵ�������ͻ��ˣ��ùܵ������� STDOUT */
        while (read(cgi_output[0], &c, 1) > 0)
            send(client, &c, 1, 0);

		/*�رչܵ�*/ 
        close(cgi_output[0]);
        close(cgi_input[1]);
		/*�ȴ��ӽ���*/ 
        waitpid(pid, &status, 0);
    }
}

/**********************************************************************/
/* ���׽��ֶ�ȡһ��,�����Ƿ��Ի��з�,�س�,����CRLF��Ͻ�����null��ֹ�ַ�����ȡ��
   ���û���ڶ���������֮ǰ�ҵ�����ָʾ��,����һ��NULL��ֹ�ַ���.
   ����������������н�����,��������'\r','\n',����'\r\n'��β��ת��Ϊ��'\n'�ټ�'\0'�ַ�������
 * ����: socket :�׽���������
 *       buf:    ������������
 *       size:   ָ��buffer�Ĵ�С(��ȡ�ĸ������Ϊsize-1,�����null����)
 * ����: �洢���ֽ��� (������ null) */
/**********************************************************************/
int get_line(int sock, char *buf, int size)
{
    int i = 0;
    char c = '\0';
    int n;

    while ((i < size - 1) && (c != '\n')) {
        n = recv(sock, &c, 1, 0);//��sock��һ�ζ�һ���ַ�
        /* DEBUG printf("%02X\n", c); */
        if (n > 0) {
            if (c == '\r') {
				/*��MSG_PEEK��ʽrecv(),����tcp buffer�е����ݶ�ȡ��buf��,
				  �������Ѷ�ȡ�����ݴ�tcp buffer���Ƴ�,�ٴε���recv��Ȼ���Զ����ղŶ��������ݡ�*/
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
/* ����HTTPͷ��Ϣ. */
/* Parameters: client:�׽���
 *             filename:�ļ��� */
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
�� ���ͻ�����һ������404 ״̬��Ϣ����ʾ��������ݲ����ڡ�*/
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
/* ���ͳ����ļ����ͻ���.  ʹ�ñ���,������Ƿ��������򱨸档
 * Parameters: client:�׽���������
 ��������������filename:Ҫ���͵��ļ�
 */
/**********************************************************************/
void serve_file(int client, const char *filename)
{
    FILE *resource = NULL;
    int numchars = 1;
    char buf[1024];

    buf[0] = 'A';
    buf[1] = '\0';
    while ((numchars > 0) && strcmp("\n", buf))  /*��ȡ������ headers ��Ϣ*/  
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
/*���������ָ���Ķ˿��Ͽ�ʼ�����������ӡ�
 *����: port: ���*port=0 ��ʾ��̬�ķ���һ���˿�,��������Ķ˿ں�д��*port�С� 
 *            *port=����:���������ӵĶ˿ں�
 * ����: �����׽��� */
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
    if (*port == 0) { /* �����̬����˿� */
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
/* ֪ͨ�ͻ����������web������û��ʵ��
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

    server_sock = startup(&port);//���������socket���󶨡�����
    printf("httpd running on port %d\n", port);

    while (1) {
		//�ȴ�����
        client_sock = accept(server_sock, (struct sockaddr *)&client_name, &client_name_len);
        if (client_sock == -1)
            error_die("accept");

        /* �����̴߳�������accept_request(client_sock)�������ڴ�������; */
        if (pthread_create(&newthread , NULL, accept_request,  (void*)&client_sock) != 0)
            perror("pthread_create");
    }

    close(server_sock);

    return(0);
}



