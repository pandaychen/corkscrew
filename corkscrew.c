#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <assert.h>

#if HAVE_SYS_FILIO_H
#include <sys/filio.h>
#endif

#if __STDC__
#  ifndef NOPROTOS
#    define PARAMS(args)      args
#  endif
#endif
#ifndef PARAMS
#  define PARAMS(args)        ()
#endif

char *base64_encodei PARAMS((char *in));
void usage PARAMS((void));
int sock_connect PARAMS((const char *hname, int port));
int main PARAMS((int argc, char *argv[]));

#define BUFSIZE 4096
#define PROXY_IP_LEN 128
#define PROXY_PORT_LEN 6
#define VERSION "pv1"

//add proxy struct
struct __proxy{
	char szProxyip[PROXY_IP_LEN];
	char szProxyport[PROXY_PORT_LEN];
};

typedef struct __proxy Proxy;

/*
char linefeed[] = "\x0A\x0D\x0A\x0D";
*/
char linefeed[] = "\r\n\r\n"; /* it is better and tested with oops & squid */

/*
** base64.c
** Copyright (C) 2001 Tamas SZERB <toma@rulez.org>
*/

const static char base64[64] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* the output will be allocated automagically */
#ifdef ANSI_FUNC
char *base64_encode (char *in)
#else
char * base64_encode (in)
char *in;
#endif
{
	char *src, *end;
	char *buf, *ret;

	unsigned int tmp;

	int i,len;

	len = strlen(in);
	if (!in)
		return NULL;
	else
		len = strlen(in);

	end = in + len;

	buf = malloc(4 * ((len + 2) / 3) + 1);
	if (!buf)
		return NULL;
	ret = buf;


	for (src = in; src < end - 3;) {
		tmp = *src++ << 24;
		tmp |= *src++ << 16;
		tmp |= *src++ << 8;

		*buf++ = base64[tmp >> 26];
		tmp <<= 6;
		*buf++ = base64[tmp >> 26];
		tmp <<= 6;
		*buf++ = base64[tmp >> 26];
		tmp <<= 6;
		*buf++ = base64[tmp >> 26];
	}

	tmp = 0;
	for (i = 0; src < end; i++)
		tmp |= *src++ << (24 - 8 * i);

	switch (i) {
		case 3:
			*buf++ = base64[tmp >> 26];
			tmp <<= 6;
			*buf++ = base64[tmp >> 26];
			tmp <<= 6;
			*buf++ = base64[tmp >> 26];
			tmp <<= 6;
			*buf++ = base64[tmp >> 26];
		break;
		case 2:
			*buf++ = base64[tmp >> 26];
			tmp <<= 6;
			*buf++ = base64[tmp >> 26];
			tmp <<= 6;
			*buf++ = base64[tmp >> 26];
			*buf++ = '=';
		break;
		case 1:
			*buf++ = base64[tmp >> 26];
			tmp <<= 6;
			*buf++ = base64[tmp >> 26];
			*buf++ = '=';
			*buf++ = '=';
		break;
	}

	*buf = 0;
	return ret;
}

#ifdef ANSI_FUNC
void usage (void)
#else
void usage ()
#endif
{
	printf("corkscrew %s (agroman@agroman.net)\n\n", VERSION);
	printf("usage: corkscrew <proxyhost1> <proxyport1> <proxyhost2> <proxyport2>... <desthost> <destport> [authfile]\n");
}

#ifdef ANSI_FUNC
int sock_connect (const char *hname, int port)
#else
int sock_connect (hname, port)
const char *hname;
int port;
#endif
{
	int fd;
	struct sockaddr_in addr;
	struct hostent *hent;

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd == -1)
		return -1;

	hent = gethostbyname(hname);
	if (hent == NULL)
		addr.sin_addr.s_addr = inet_addr(hname);
	else
		memcpy(&addr.sin_addr, hent->h_addr, hent->h_length);
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);

	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)))
		return -1;

	return fd;
}

#ifdef ANSI_FUNC
int main (int argc, char *argv[])
#else
int main (argc, argv)
int argc;
char *argv[];
#endif
{
#ifdef ANSI_FUNC
	char uri[BUFSIZE] = "", buffer[BUFSIZE] = "", version[BUFSIZE] = "", descr[BUFSIZE] = "";
#else
	char uri[BUFSIZE], buffer[BUFSIZE], version[BUFSIZE], descr[BUFSIZE];
#endif
	char *host = NULL, *desthost = NULL, *destport = NULL;
	char *up = NULL, line[4096];
	int port, sent, setup, code, csock;
	fd_set rfd, sfd;
	struct timeval tv;
	ssize_t len;
	FILE *fp;

	int proxy_num = 0;
	int succ_proxy_count = 0;

	port = 80;

	Proxy *psshproxy = NULL;

	//ssh -o "ProxyCommand corkscrew proxyhostname(代理IP) proxyport(代理port) %h %p" loginname@ip
	if (argc < 3){
		usage();
		exit(-1);
	}

	proxy_num = (argc-3)/2;

	//cancel proxy auth
	psshproxy = (Proxy *)malloc(proxy_num*sizeof(*psshproxy));
	assert(psshproxy != NULL);
	int i = 0;
	for (; i < proxy_num; i++){
		snprintf(psshproxy[i].szProxyip, sizeof(psshproxy[i].szProxyip), "%s", argv[1 + 2*i]);
		snprintf(psshproxy[i].szProxyport, sizeof(psshproxy[i].szProxyport), "%s", argv[1 + 2 * i + 1]);
	}


	for (i = 0; i < proxy_num; i++){
		printf("%s,%d\n", psshproxy[i].szProxyip, psshproxy[i].szProxyport);
	}

	desthost = argv[1+2*i];			//要连接的目的IP
	destport = argv[1+2*i+1];		//要连接的目的PORT

	/*	拼接CONNECT URL
		url:CONNECT 1.2.3.4:36000 HTTP/1.0
		response:HTTP / 1.1 200 Connection established
	*/
	strncpy(uri, "CONNECT ", sizeof(uri));
	strncat(uri, desthost, sizeof(uri) - strlen(uri) - 1);
	strncat(uri, ":", sizeof(uri) - strlen(uri) - 1);
	strncat(uri, destport, sizeof(uri) - strlen(uri) - 1);
	strncat(uri, " HTTP/1.0", sizeof(uri) - strlen(uri) - 1);
	if (up != NULL) {
		strncat(uri, "\nProxy-Authorization: Basic ", sizeof(uri) - strlen(uri) - 1);
		strncat(uri, base64_encode(up), sizeof(uri) - strlen(uri) - 1);
	}
	strncat(uri, linefeed, sizeof(uri) - strlen(uri) - 1);

	//CONNECT THE FIRST SERVER
	csock = sock_connect(psshproxy[0].szProxyip, atoi(psshproxy[0].szProxyport));
	if(csock == -1) {
		fprintf(stderr, "Couldn't establish connection to proxy: %s\n", strerror(errno));
		exit(-1);
	}

	//proxy connect
	sent = 0;	
	setup = 0;	
	succ_proxy_count++;
	for (;;){
		FD_ZERO(&sfd);
		FD_ZERO(&rfd);

		if (setup == 1 && sent == 1){
			succ_proxy_count++;
			setup = 0;
			sent = 0;
		}

		if (succ_proxy_count > proxy_num){
			break;
		}

		memset(uri, 0, BUFSIZE);
		if (succ_proxy_count == proxy_num){
			//connect to real server
			strncpy(uri, "CONNECT ", sizeof(uri));
			strncat(uri, desthost, sizeof(uri)-strlen(uri) - 1);
			strncat(uri, ":", sizeof(uri)-strlen(uri) - 1);
			strncat(uri, destport, sizeof(uri)-strlen(uri) - 1);
			strncat(uri, " HTTP/1.0", sizeof(uri)-strlen(uri) - 1);
			strncat(uri, linefeed, sizeof(uri)-strlen(uri) - 1);
		}
		else{
			strncpy(uri, "CONNECT ", sizeof(uri));
			strncat(uri, psshproxy[succ_proxy_count].szProxyip, sizeof(uri)-strlen(uri) - 1);
			strncat(uri, ":", sizeof(uri)-strlen(uri) - 1);
			strncat(uri, psshproxy[succ_proxy_count].szProxyport, sizeof(uri)-strlen(uri) - 1);
			strncat(uri, " HTTP/1.0", sizeof(uri)-strlen(uri) - 1);
			strncat(uri, linefeed, sizeof(uri)-strlen(uri) - 1);
		}

		if ((setup == 0) && (sent == 0) ) {
			FD_SET(csock, &sfd);
		}
		FD_SET(csock, &rfd);
		FD_SET(0, &rfd);

		tv.tv_sec = 5;
		tv.tv_usec = 0;
		if (select(csock + 1, &rfd, &sfd, NULL, &tv) == -1) break;

		/* there's probably a better way to do this */
		if (setup == 0) {
			//接收数据
			if (FD_ISSET(csock, &rfd)) {
				len = read(csock, buffer, sizeof(buffer));
				if (len <= 0)
					break;
				else {
					sscanf(buffer, "%s%d%[^\n]", version, &code, descr);
					if ((strncmp(version, "HTTP/", 5) == 0) && (code >= 200) && (code < 300)){
						setup = 1;
					}
					else {
						if ((strncmp(version, "HTTP/", 5) == 0) && (code >= 407)) {
						}
						fprintf(stderr, "Proxy could not open connnection to %s: %s\n", desthost, descr);
						exit(-1);
					}
				}
			}
			if (FD_ISSET(csock, &sfd) && (sent == 0)) {
				len = write(csock, uri, strlen(uri));
				if (len <= 0)
					break;
				else
					sent = 1;
			}
		}
	}

	//proxy connect done
	for(;;) {
		FD_ZERO(&sfd);
		FD_ZERO(&rfd);
		FD_SET(csock, &rfd);
		FD_SET(0, &rfd);

		tv.tv_sec = 5;
		tv.tv_usec = 0;
			
		if(select(csock+1,&rfd,&sfd,NULL,&tv) == -1) break;

	
		//forwarding dataflow
		if (FD_ISSET(csock, &rfd)) {
			len = read(csock, buffer, sizeof(buffer));
			if (len<=0) break;
			len = write(1, buffer, len);
			if (len<=0) break;
		}

		if (FD_ISSET(0, &rfd)) {
			//0
			len = read(0, buffer, sizeof(buffer));
			if (len<=0) break;
			len = write(csock, buffer, len);
			if (len<=0) break;
		}	
	}

	if (psshproxy)	free(psshproxy);
	exit(0);
}
