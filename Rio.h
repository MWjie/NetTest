#ifndef _RIO_H_
#define _RIO_H_

#include <sys/types.h>

#define RIO_BUFSIZE    ( 4096 )


typedef struct {
    int rio_fd;                //���ڲ�������������������
    int rio_cnt;               //��������ʣ�µ��ֽ���
    char *rio_bufptr;          //ָ�򻺳�������һ��δ�����ֽ�
    char rio_buf[RIO_BUFSIZE]; 
} rio_t;

ssize_t rio_readn(int fd, void *usrbuf, size_t n);
ssize_t rio_writen(int fd, void *usrbuf, size_t n);
void rio_readinitb(rio_t *rp, int fd);
ssize_t rio_readlineb(rio_t *rp, void *usrbuf, size_t maxlen);
ssize_t rio_readnb(rio_t *rp, void *usrbuf, size_t n); 



#endif //_RIO_H_

