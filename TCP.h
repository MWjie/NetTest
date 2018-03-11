#ifndef _TCP_H_
#define _TCP_H_

int open_clientfd(char *hostname, unsigned short port);
int open_listenfd(char *hostname, unsigned short port);

#endif //_TCP_H_
