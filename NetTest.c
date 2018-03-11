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

/* IP��Ϣ */
typedef struct ConnInfo {
    char ip[16];                //IP��ַ
    unsigned short port;        //�˿�
    int connfd;                 //�׽���
} ConnInfo;

/* �˿�״̬ */
typedef struct stNetPacket {
    ConnInfo IP_info;
    unsigned int tx_lens;       //�������ݸ���
    unsigned int rx_lens;       //�������ݸ���
    unsigned int tx_pkgs;       //�������ݰ�����
    unsigned int rx_pkgs;       //�������ݰ�����
    unsigned int err_lens;      //�������ݸ���
    unsigned int err_pkgs;      //�������ݰ�����
    unsigned int lost_lens;     //��ʧ���ݸ���
    unsigned int lost_pkgs;     //��ʧ���ݰ����� 
    unsigned int timeout;       //���ճ�ʱʱ��
    float accuracy;             //׼ȷ��
    pthread_t id;               //�̺߳�
    char st_exit;               //֪ͨ���̽���
} stNetPacket;

/* ״̬�� */
struct LinkNode_Net;
typedef struct LinkNode_Net* pLinkNode_Net;
typedef struct LinkNode_Net {
    stNetPacket *pNetStatus;
    pLinkNode_Net next;
} LinkNode_Net;

#define MAXLINE         ( 256 )         //������������ֽ�
#define CLOCKID			( CLOCK_REALTIME )

int RX_TIME_OUT_MS = 1000;      //��ʱʱ��
LinkNode_Net NetMapLink;        //״̬��
char Flag_Start = 0;            //�߳�������־

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
    unsigned short start_port, num_port;
    char dest_ip[16];
    stNetPacket *pLinkNode;
    pLinkNode_Net plink = &NetMapLink;
    stNetPacket *plinkStatus;
    timer_t timerid;
    time_t start_time, now_time;
    struct tm *timeinfo;
    struct sigevent evp;
    memset(&evp, 0, sizeof(struct sigevent)); //�����ʼ��  
	char tmp_printf[4096];
    char tmp_str[512];
    
	if (argc != 4) {
        fprintf(stderr, "usage: %s [ip] [port] [num]\n", argv[0]);
        exit(-1);
	}
    strcpy(dest_ip, argv[1]); //Ŀ��ip
    start_port = (unsigned short)atoi(argv[2]); //��ʼ�˿ں�
    num_port = (unsigned short)atoi(argv[3]);   //�˿ں�����
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
        printf("\033[H""\033[J"); //����
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
            plinkStatus->accuracy = (float)(plinkStatus->rx_lens - plinkStatus->err_lens - plinkStatus->lost_lens)
                                        /(float)plinkStatus->rx_lens*100;
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
    char rx_buf[MAXLINE];
    char tx_buf[MAXLINE];
    rio_t rio;
    int i, count;
    char is_frame, is_errframe;
    stNetPacket *pNet = pNetStatus;
    memset(rx_buf, 0, sizeof(rx_buf));
    for (i = 0; i < MAXLINE; i++) {
        tx_buf[i] = i;
    }

    while (1) {
        if (masterORslave == 0) { //���豸
            rio_writen(pNet->IP_info.connfd, tx_buf, n);
            __sync_lock_test_and_set(&(pNet->timeout), 0);
            pNet->tx_pkgs++;
            pNet->tx_lens +=256;
            rio_readinitb(&rio, pNet->IP_info.connfd);
            while (1) {
                if (pNet->timeout > RX_TIME_OUT_MS) {
                    pNet->lost_lens += 256;
                    is_errframe = 1;
                    continue;
                }
                if ((n = rio_readlineb(&rio, rx_buf, MAXLINE)) > 0) {
                    __sync_lock_test_and_set(&(pNet->timeout), 0);
                    break;
                }
                usleep(1000);
            }
            for (i = 0; i < n; i++) {
                if (rx_buf[i] == 0x00) {
                    count = 0;
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
                if (rx_buf[i] == 0xff) {
                    pNet->rx_pkgs++;
                }
                if (is_errframe) {
                    pNet->err_pkgs++;
                }
            }
            if (pNet->st_exit == 1) {
                exit(1);
            }
            sleep(1);
        } 
        else { //���豸
            if (pNet->timeout > RX_TIME_OUT_MS) {
                pNet->lost_lens += 256;
                is_errframe = 1;
                continue;
            }
            rio_readinitb(&rio, pNet->IP_info.connfd);
            if ((n = rio_readlineb(&rio, rx_buf, MAXLINE)) > 0) {
                __sync_lock_test_and_set(&(pNet->timeout), 0);
                for (i = 0; i < n; i++) {
                    if (rx_buf[i] == 0x00) {
                        count = 0;
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
                    if (rx_buf[i] == 0xff) {
                        pNet->rx_pkgs++;
                    }
                    if (is_errframe) {
                        pNet->err_pkgs++;
                    }
                }
                rio_writen(pNet->IP_info.connfd, tx_buf, n);
                __sync_lock_test_and_set(&(pNet->timeout), 0);
                pNet->tx_pkgs++;
                pNet->tx_lens += 256;
            }
            if (pNet->st_exit == 1) {
                exit(1);
            }
            usleep(1000);
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

