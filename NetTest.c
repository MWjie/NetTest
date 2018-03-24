#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <pthread.h>
#include "Rio.h"

/* IP信息 */
typedef struct ConnInfo {
    char ip[16];                //IP地址
    unsigned short port;        //端口
    int connfd;                 //套接字
} ConnInfo;

/* 端口状态 */
typedef struct stNetPacket {
    ConnInfo IP_info;
    unsigned int tx_lens;       //发送数据个数
    unsigned int rx_lens;       //接收数据个数
    unsigned int tx_pkgs;       //发送数据包个数
    unsigned int rx_pkgs;       //接收数据包个数
    unsigned int err_lens;      //错误数据个数
    unsigned int err_pkgs;      //错误数据包个数
    unsigned int lost_lens;     //丢失数据个数
    unsigned int lost_pkgs;     //丢失数据包个数 
    unsigned int timeout;       //接收超时时间
    float accuracy;             //准确率
    pthread_t id;               //线程号
    char st_exit;               //通知进程结束
} stNetPacket;

/* 状态表 */
struct LinkNode_Net;
typedef struct LinkNode_Net* pLinkNode_Net;
typedef struct LinkNode_Net {
    stNetPacket *pNetStatus;
    pLinkNode_Net next;
} LinkNode_Net;

#define MAXLINE         ( 256 )         //发送最大数据字节
#define CLOCKID			( CLOCK_REALTIME )

int RX_TIME_OUT_MS;      //超时时间
LinkNode_Net NetMapLink;        //状态表
char Flag_Start = 0;            //线程启动标志

int open_clientfd(char *hostname, unsigned short port);
int insertNetNode(stNetPacket *pLinkNode);
void *thread(void *vargp);
void echo(int masterORslave, stNetPacket *pNetStatus);
void PrintLink(void);


void my_handler_ctrl_c(int s) 
{
    pLinkNode_Net plink = &NetMapLink;
    while (1) {
        close(plink->pNetStatus->IP_info.connfd);
        plink->pNetStatus->st_exit = 1;
        if (plink->next == NULL) {
            break;
        }
        plink = plink->next;
    }
	printf("Caught signal %d\n", s);
} 

void timer_thread(union sigval v)
{  
	pLinkNode_Net plink = &NetMapLink;
    while (1) {
        __sync_fetch_and_add(&(plink->pNetStatus->timeout), 1);
        if (plink->next == NULL) {
            break;
        }
        plink = plink->next;
    }
}

int main(int argc, char const *argv[])
{
    int i, *pid;
    unsigned short start_port, num_port, bundrate;
    unsigned int tx_count;
    char dest_ip[16];
    stNetPacket *pLinkNode;
    pLinkNode_Net plink = &NetMapLink;
    stNetPacket *plinkStatus;
    timer_t timerid;
    time_t start_time, now_time;
    struct tm *timeinfo;
    struct sigevent evp;
    memset(&evp, 0, sizeof(struct sigevent)); //清零初始化  
	char tmp_printf[4096];
    char tmp_str[512];
    
	if (argc != 5) {
        fprintf(stderr, "usage: %s [ip] [port] [num] [bundrate]\n", argv[0]);
        exit(-1);
	}
    strcpy(dest_ip, argv[1]); //目标ip
    start_port = (unsigned short)atoi(argv[2]); //起始端口号
    num_port = (unsigned short)atoi(argv[3]);   //端口号总数
    bundrate = (unsigned short)atoi(argv[4]);   //波特率
    RX_TIME_OUT_MS = (1*256*10*8*1000)/bundrate;
    if (num_port == 0) {
        fprintf(stderr, "port number must > 0\n");
        exit(1);
    }
    printf("process name: %s, ip: %s, port: %d, num: %d\n", argv[0], dest_ip, start_port, num_port);
    
    for (i = 0; i < num_port; i++) {
        pid = (int *)malloc(sizeof(int));
        *pid = i;
        pLinkNode = (stNetPacket *)malloc(sizeof(stNetPacket));
        memset(pLinkNode, 0, sizeof(stNetPacket));
        strcpy(pLinkNode->IP_info.ip, dest_ip);
        pLinkNode->IP_info.port = start_port + i;
        if ((pLinkNode->IP_info.connfd = open_clientfd(pLinkNode->IP_info.ip, pLinkNode->IP_info.port)) < 0) {
            fprintf(stderr, "open ip: %s, port:%d error\n", pLinkNode->IP_info.ip, pLinkNode->IP_info.port);
            exit(1);
        }
        printf("IP_info.ip: %s, IP_info.port: %d, connfd: %d\n", pLinkNode->IP_info.ip, pLinkNode->IP_info.port, pLinkNode->IP_info.connfd);
        if ((pthread_create(&(pLinkNode->id), NULL, thread, pid)) != 0) {
            fprintf(stderr, "Create pthread error\n");
            exit(1);
        }
        if ((insertNetNode(pLinkNode)) < 0) {
            printf("Node is NULL\n");
        }        
    }
    PrintLink();

    struct sigaction sigIntHandler;
    sigIntHandler.sa_handler = my_handler_ctrl_c;  
	sigemptyset(&sigIntHandler.sa_mask);  
	sigIntHandler.sa_flags = 0;  
	sigaction(SIGINT, &sigIntHandler, NULL);  

    evp.sigev_value.sival_int = 111;      
    evp.sigev_notify = SIGEV_THREAD;           
    evp.sigev_notify_function = timer_thread;   

    struct itimerspec it;  
    it.it_interval.tv_sec = 0;  
    it.it_interval.tv_nsec = 1*1000*1000; //1mS
    it.it_value.tv_sec = 0;  
    it.it_value.tv_nsec = 1*1000*1000; //1mS  

    if (timer_create(CLOCKID, &evp, &timerid) == -1) {  
        fprintf(stderr, "fail to timer_create\n");  
        exit(-1);  
    } 
  
    if (timer_settime(timerid, 0, &it, NULL) == -1) {  
        fprintf(stderr, "fail to timer_settime\n");  
        exit(-1);  
    } 
    
    time(&start_time);
    Flag_Start = 1;
	sleep(1);
    
    while (1) {
        printf("\033[H""\033[J"); //清屏
		time(&now_time);
        plink = &NetMapLink;
        if (plink->pNetStatus->st_exit == 1) {
            exit(1);
        }

		sprintf(tmp_printf, "Test App time out %dmS Test time %ldmin %ldSec\n\r",  RX_TIME_OUT_MS,
			  (now_time - start_time)/60, (now_time - start_time)%60);
		timeinfo = localtime(&start_time);
		sprintf(tmp_str, "App start time -- %s", asctime(timeinfo));
		strcat(tmp_printf, tmp_str);
		timeinfo = localtime(&now_time);
		sprintf(tmp_str, "Now time -- %s", asctime(timeinfo));
		strcat(tmp_printf, tmp_str);	  
		sprintf(tmp_str, "PORT  states    tx_lens     rx_lens     tx_pkgs     rx_pkgs     err_pkgs     err_lens    lost_pkgs    lost_lens  accuracy\n\r");
		strcat(tmp_printf, tmp_str);

        while (1) {
            plinkStatus = plink->pNetStatus;
            if (plinkStatus->IP_info.port % 2) {
                tx_count = plinkStatus->tx_lens;
            }
            plinkStatus->accuracy = (float)(tx_count - plinkStatus->err_lens - plinkStatus->lost_lens)
                                        /(float)tx_count*100;
            sprintf(tmp_str, "%-4d       ", plinkStatus->IP_info.port);
			strcat(tmp_printf, tmp_str);
            if (plinkStatus->accuracy < 100) {
                sprintf(tmp_str, "%10d  %10d  %10d  %10d   %10d   %10d   %10d   %10d     \033[1;31;40m%.2f%%\033[0m\n\r",
					plinkStatus->tx_lens,   plinkStatus->rx_lens, 
					plinkStatus->tx_pkgs,   plinkStatus->rx_pkgs, 
					plinkStatus->err_pkgs,  plinkStatus->err_lens, 
					plinkStatus->lost_pkgs, plinkStatus->lost_lens, plinkStatus->accuracy);					
            } else {
                sprintf(tmp_str, "%10d  %10d  %10d  %10d   %10d   %10d   %10d   %10d     \033[1;32;40m%.2f%%\033[0m\n\r",
					plinkStatus->tx_lens,   plinkStatus->rx_lens, 
					plinkStatus->tx_pkgs,   plinkStatus->rx_pkgs, 
					plinkStatus->err_pkgs,  plinkStatus->err_lens, 
					plinkStatus->lost_pkgs, plinkStatus->lost_lens, plinkStatus->accuracy);
            }
            strcat(tmp_printf, tmp_str);
            if (plink->next == NULL) {
                break;
            }
            plink = plink->next;
        }

        strcat(tmp_printf, "\0");
		printf("%s", tmp_printf);
        sleep(1);
    }
    
    return 0;
}

void *thread(void *vargp) 
{
    int id = *((int *)vargp);   
    free(vargp);
    printf("thread %d create\n", id);
    pthread_detach(pthread_self());
    while (!Flag_Start) {
        usleep(1000);
    }
    int i;
    pLinkNode_Net plink = &NetMapLink;
    for (i = 0; i < id; i++) {
        if (plink == NULL) {
            printf("plink is NULL\n");
            return 0;
        }
        plink = plink->next;
    }
    echo(id % 2, plink->pNetStatus);
    exit(0);
}

int open_clientfd(char *hostname, unsigned short port)
{
    int clientfd;
    int flags;
    struct hostent *hp;
    struct sockaddr_in serveraddr;
    
    if ((clientfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        return -1; /* Check errno for cause of error */
    }
    /* Fill in the server's IP address and port */
    if ((hp = gethostbyname(hostname)) == NULL) {
        return -2; /* Check h_errno for cause of error */
    }
    bzero((char *)&serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    bcopy((char *)hp->h_addr_list[0], (char *)&serveraddr.sin_addr.s_addr, hp->h_length);
    serveraddr.sin_port = htons(port);

    flags = fcntl(clientfd, F_GETFL, 0);
    fcntl(clientfd, flags | O_NONBLOCK);

    /* Establish a connection with the server */
    if (connect(clientfd, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) < 0) {
        return -3;
    }
    return clientfd;
}

int insertNetNode(stNetPacket *pLinkNode)
{
    LinkNode_Net *pNetMapLink = &NetMapLink;
    pLinkNode_Net pnext;
    if (pLinkNode == NULL) {
        return -1;
    }
    if (pNetMapLink->pNetStatus == NULL) {
        pNetMapLink->pNetStatus = pLinkNode;
    } else {
        while (pNetMapLink->next != NULL) {
            pNetMapLink = pNetMapLink->next;
        }
        pnext = (LinkNode_Net *)malloc(sizeof(LinkNode_Net));
        pnext->pNetStatus = pLinkNode;
        pnext->next = NULL;
        pNetMapLink->next = pnext;
    }
}

void echo(int masterORslave, stNetPacket *pNetStatus)
{
    size_t n;
    unsigned char rx_buf[MAXLINE];
    unsigned char tx_buf[MAXLINE];
    rio_t rio;
//    fd_set fdRead;
//    int rc = 0;
//    struct timeval tv;  
//    tv.tv_sec  = 5;
//    tv.tv_usec = 0;
    int i, count;
    char is_frame, is_errframe;
    stNetPacket *pNet = pNetStatus;

    memset(rx_buf, 0, sizeof(rx_buf));
    for (i = 0; i < MAXLINE; i++) {
        tx_buf[i] = i;
    }

    while (1) {
        is_errframe = 0;
        is_frame = 0;
        count = 0;
        
        if (masterORslave == 0) { //主设备
            rio_writen(pNet->IP_info.connfd, tx_buf, sizeof(tx_buf));
            __sync_lock_test_and_set(&(pNet->timeout), 0);
            pNet->tx_pkgs++;
            pNet->tx_lens +=256;
            rio_readinitb(&rio, pNet->IP_info.connfd);
            while (1) {
                if (pNet->timeout > RX_TIME_OUT_MS) {
                    pNet->lost_lens += 256;
                    pNet->lost_pkgs++;
                    is_errframe = 1;
                    __sync_lock_test_and_set(&(pNet->timeout), 0);
                    break;
                }
/*                
                FD_ZERO(&fdRead);
                FD_SET(rio.rio_fd, &fdRead);
                tv.tv_sec  = 5;
                tv.tv_usec = 0;
                rc = select(0, &fdRead, NULL, NULL, &tv);
                if (rc == 0) { //超时
                    pNet->lost_lens += 256;
                    is_errframe = 1;
                    break;
                } 
*/
                //if(pNet->IP_info.port == 5101)
//                    printf("%d\n", pNet->IP_info.port);
                if ((n = rio_readnb(&rio, rx_buf, MAXLINE)) > 0) {
                    __sync_lock_test_and_set(&(pNet->timeout), 0);
                    break;
                }
                usleep(1000);
            }
            for (i = 0; i < n; i++) {
                if (rx_buf[i] == 0x00) {
//                    if (is_frame == 1) {
//                        is_errframe = 1;
//                        pNet->err_lens += n;
//                        pNet->rx_lens  += n;
//                        break;
//                    }
//                    count = 0;
                    is_frame = 1;
                }
                if (is_frame && count < sizeof(rx_buf)) {
                    if (rx_buf[i] != count) {
                        pNet->err_lens++;
                        is_errframe = 1;
                    }
                    count++;
                }
                pNet->rx_lens++;
                if (i == 0xff && rx_buf[i] == 0xff) {
                    pNet->rx_pkgs++;
                } 
                if (!is_frame && i >= n - 1) {
                    pNet->err_lens += n;
                    is_errframe = 1;
                }
            }
            if (is_errframe) {
                pNet->err_pkgs++;
            }
            if (pNet->st_exit == 1) {
                exit(1);
            }
            usleep(100);
        } 
        else { //从设备
            rio_readinitb(&rio, pNet->IP_info.connfd);
            while (1) {
                if (pNet->timeout > RX_TIME_OUT_MS) {
                    pNet->lost_lens += 256;
                    pNet->lost_pkgs++;
                    is_errframe = 1;
                    __sync_lock_test_and_set(&(pNet->timeout), 0);
                    break;
                }
/*
            FD_ZERO(&fdRead);
            FD_SET(rio.rio_fd, &fdRead);
            tv.tv_sec  = 5;
            tv.tv_usec = 0;
            rc = select(0, &fdRead, NULL, NULL, &tv);
            if (rc == 0) { //超时
                pNet->lost_lens += 256;
                is_errframe = 1;
                continue;
            }
*/
//            if(pNet->IP_info.port == 5101)
//                printf("%d\n", pNet->IP_info.port);
                if ((n = rio_readnb(&rio, rx_buf, MAXLINE)) > 0) {
                    __sync_lock_test_and_set(&(pNet->timeout), 0);
                    break;
                }
                usleep(1000);
            }

            for (i = 0; i < n; i++) {
                if (rx_buf[i] == 0x00) {
//                    if (is_frame == 1) {
//                        is_errframe = 1;
//                        pNet->err_lens += n;
//                        pNet->rx_lens  += n;
//                        break;
//                    }
//                    count = 0;
                    is_frame = 1;
                }
                if (is_frame && count < sizeof(rx_buf)) {
                    if (rx_buf[i] != count) {
                        pNet->err_lens++;
                        is_errframe = 1;
                    }
                    count++;
                }
                
                pNet->rx_lens++;
                if (i == 0xff && rx_buf[i] == 0xff) {
                    pNet->rx_pkgs++;
                }
                if (!is_frame && i >= n - 1) {
                    pNet->err_lens += n;
                    is_errframe = 1;
                }
            }
            if (is_errframe) {
                pNet->err_pkgs++;
            }
            if (n >= MAXLINE-1) {
                rio_writen(pNet->IP_info.connfd, tx_buf, sizeof(tx_buf));
                __sync_lock_test_and_set(&(pNet->timeout), 0);
                pNet->tx_pkgs++;
                pNet->tx_lens += 256;
            }
            if (pNet->st_exit == 1) {
                exit(1);
            }
            usleep(100);
        }
    }
}

void PrintLink(void)
{
    pLinkNode_Net plink = &NetMapLink;
    while (1) {
        printf("ip: %s, port: %d\n", plink->pNetStatus->IP_info.ip, (int)(plink->pNetStatus->IP_info.port));
        if (plink->next == NULL) {
            break;
        }
        plink = plink->next;
    }
}

