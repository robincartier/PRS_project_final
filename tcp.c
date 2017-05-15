#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/time.h>
#include <time.h>
#include <pthread.h>
#include "data.h"
#include "tcp.h"
#include <errno.h>

//SRTT(k)=ALPHA*SRTT(k-1) + (1-ALPHA)*RTT(k-1)
double srtt_next(double srtt, double rtt) {
  return (ALPHA*srtt) + ((1-ALPHA)*rtt);
}


void init_seg(int num_seg, char *buffer) {
  strcpy(buffer, "000000");
  char temp[SEGSIZE+1]="000000\n";
  sprintf(temp, "%d", num_seg);
  int x=0, i;

  for(i=6; i>=0; i--)
  {
    if(x != 0)
    {
      buffer[SEGSIZE-x+i]=temp[i];
    }
    if(temp[i]=='\0') x = i;
  }
}


void test_ack(int *ack_tab, char *buffer_ack, int seg_rcv, int resend, int cwnd) { // a optimiser
  char temp[SEGSIZE+1];
  int i;
  int num=0;
  char *a;
  for(i=0; i<SEGSIZE; i++) //recuperation du ACK recu
  {
    temp[i]=buffer_ack[3+i];
  }
  temp[SEGSIZE] = '\0';
  num = atoi(temp); //SEG FAULT

  printf("ack rcv : %d\n", num);
  if(num>seg_rcv && num<=seg_rcv+cwnd) {
    ack_tab[num-seg_rcv]=1;
    for(i=1; i<=(num-seg_rcv); i++) {
      ack_tab[i]=1;
    }

    int rcv_all=1;
    for(i=1; i<=cwnd; i++) {
      if(ack_tab[i]==0) rcv_all=0;
    }
    if(rcv_all==1)  ack_tab[0]=1;
  }

}


int receive(char *buffer_ack, int *ack_tab, int seg_rcv, int resend, int desc_data, struct sockaddr_in *com, socklen_t alen, struct timeval *time_init, int timeout, int cwnd) {
  memset(buffer_ack, 0, ACKSIZE);
  struct timeval time_iter, time_diff, time_limit;
  time_iter.tv_sec = 0;
  time_diff.tv_sec = 0;
  int test;



  int x; int tps;

  // do { //attente de l'ACK pendant srtt*TIMEOUT
  //   x = (int) recvfrom(desc_data, buffer_ack, ACKSIZE, MSG_DONTWAIT, (struct sockaddr *)com, &alen);
  //   if (x == -1) {
  //   //  int errsv = errno;
  //   //  if(errsv !=11)
  //   //  printf("receiving failed : %d\n", errsv);
  //   }
  //   gettimeofday(&time_iter, NULL);
  //   timersub(&time_iter, time_init, &time_diff);
  //   if(x != -1) {
  //     test_ack(ack_tab, buffer_ack, seg_rcv, resend, cwnd);
  //   }
  //   tps = time_diff.tv_usec;
  // } while(ack_tab[0]==0 && tps < timeout); //faire dépendre de la fenetre


  do { //attente de l'ACK pendant srtt*TIMEOUT
    time_limit.tv_sec = 0;
    //time_limit.tv_usec = 0;
    time_limit.tv_usec = timeout;
    fd_set desc_set;
    FD_ZERO(&desc_set);
    FD_SET(desc_data, &desc_set);
    test = select(20, &desc_set, NULL, NULL, &time_limit);
    //printf("test : %d, %ld, %d\n", test,  time_limit.tv_usec, desc_data);
    if (test == -1) {
      int errsv = errno;
      printf("select fail : %d\n", errsv);
      exit(1);
    }
    if(test != 0) {
      x = (int) recvfrom(desc_data, buffer_ack, ACKSIZE, MSG_DONTWAIT, (struct sockaddr *)com, &alen);
      if (x == -1) {
        int errsv = errno;
      //  if(errsv !=11)
        printf("receiving failed : %d\n", errsv);
        exit(1);
      }
      //printf("buffer ack in receive : %s\n", buffer_ack);
      gettimeofday(&time_iter, NULL);
      timersub(&time_iter, time_init, &time_diff);
      if(x != -1) {
        test_ack(ack_tab, buffer_ack, seg_rcv, resend, cwnd);
      }
      tps = time_diff.tv_usec;
    }

  } while(ack_tab[0]==0 && test !=0); //faire dépendre de la fenetre


  //printf("\nRECOIS port : %d, adresse : %d\n", com->sin_port, com->sin_addr.s_addr);
  if(tps >= timeout) {
    printf("timeout\n" );
    return -1;
  } else {
    return tps;
  }
}



void sending(char *buffer, int num_seg, int offset, FILE *f, int desc_data, struct sockaddr_in *com, struct timeval *time_init, socklen_t alen, int cwnd) {
  //printf("\nENVOI port : %d, adresse : %d\n", com->sin_port, com->sin_addr.s_addr);
  if(offset!=0) if(fseek(f, -(MSGSIZE*(cwnd-offset+1)), SEEK_CUR)!=0) {printf("fail \n");} //fonctionne pas si c'est le dernier seg : pas se decaller de 1024
  memset(buffer, 0, MSGSIZE + SEGSIZE);
  init_seg(num_seg, buffer);
  if(offset!=0) printf("renvoi seg : %s\n", buffer);
  else printf("envoi seg : %s\n", buffer);

  int size = fread((&buffer[SEGSIZE]), (size_t)1, (size_t)MSGSIZE, f); //peut etre mis dans le "if"
  gettimeofday(time_init, NULL);
  // printf("%s\n", buffer);
  if (sendto(desc_data, buffer, MSGSIZE + SEGSIZE, 0, (struct sockaddr *)com, alen) == -1) {
    int errsv = errno;
    printf("sending failed\n");
    printf("%d\n", errsv);
  }

  if(offset!=0) if(fseek(f, (MSGSIZE*(cwnd-offset)), SEEK_CUR)!=0) {printf("fail \n");}
  // if(offset!=0) exit(1);
  //printf("ENVOI port : %d, adresse : %d\n\n", com->sin_port, com->sin_addr.s_addr);
}


int slow_start(int cwnd, int resend) {
  if(resend > 2) {
    if(cwnd>20) return (int)(cwnd/2);
  } else {
    if( (cwnd*2) < CWND_MAX) {
      return (cwnd*2);
    } else {
       return cwnd;
     }
  }
}
