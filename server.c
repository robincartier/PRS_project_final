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


void *communication(void *arg);


int main (int argc, char *argv[]) {

  int port, port_data;

  if(argc<2) {
    printf("argument 1 : num port serveur entre 1000 et 9999\n");
    exit(1);
  }
  else {
    port = atoi(argv[1]);
    if(port<1000 || port>9999) {
      printf("argument 1 : num port serveur entre 1000 et 9999\n");
      exit(1);
    }
  }

  struct sockaddr_in adresse, com;

  int valid = 1;
  char buffer[MSGSIZE]; //buffer reception

  int desc = socket(AF_INET, SOCK_DGRAM, 0); //create socket

  // handle error
  if (desc < 0) {
    perror("cannot create socket\n");
    return -1;
  }

  setsockopt(desc, SOL_SOCKET, SO_REUSEADDR, &valid, sizeof(valid));

  adresse.sin_family= AF_INET;
  adresse.sin_port= htons(port);
  adresse.sin_addr.s_addr= htonl(INADDR_ANY);
  socklen_t alen = sizeof(adresse);

  if (bind(desc, (struct sockaddr*) &adresse, sizeof(adresse)) == -1) {
    perror("Bind socket fail\n");
    close(desc);
    return -1;
  }

  fd_set desc_set;

  while (1) {
    FD_ZERO(&desc_set);
    FD_SET(desc, &desc_set);
    printf("Listening on port %d\n", port);
    select(4, &desc_set, NULL, NULL, NULL); //attention, les desc 1 et 2 sont les entrées sorties standard

    if(FD_ISSET(desc, &desc_set)) {
      int connec=0;
      while(!connec) {
        recvfrom(desc, buffer, MSGSIZE, 0, (struct sockaddr *)&com, &alen);
        if (strcmp(buffer,"SYN") == 0) {
          printf("Received message : %s\n", buffer);
          memset(buffer,0, MSGSIZE + SEGSIZE);

          srand(time(NULL));
          port_data = rand()%(9999-1000)+1000; //port aléatoire (entre 1000 et 9999)

          pthread_t t; //creation thread
          if(pthread_create(&t, NULL, communication, &port_data)!=0) {
            printf("echec de creation du thread\n");
            exit(1);
          }

          sprintf(buffer, "SYN-ACK%d", port_data);
          printf("Sending %s...\n", buffer);
          sendto(desc, buffer, MSGSIZE, 0, (struct sockaddr *)&com, sizeof(adresse));
          memset(buffer, 0, MSGSIZE + SEGSIZE);

          recvfrom(desc, buffer, MSGSIZE, 0, (struct sockaddr *)&com, &alen);
          if (strcmp(buffer,"ACK") == 0) {
            printf("Received message : %s\n", buffer);
            memset(buffer,0, MSGSIZE + SEGSIZE);
            printf("Client connected\n");
            connec=1;
          }
        }
      }
    }
  }
  close(desc);
  return 0;
}




void *communication(void *arg) {
  printf("Thread created\n");
  int *port_address = (int *) arg;
  int port = (int)*port_address;

  struct sockaddr_in *com;
  com = (struct sockaddr_in *)malloc(sizeof(struct sockaddr_in));
  com->sin_family= AF_INET;
  com->sin_port= htons(port);
  com->sin_addr.s_addr= htonl(INADDR_ANY);

  socklen_t alen = sizeof(*com);
  int valid = 1;

  int desc_data = socket(AF_INET, SOCK_DGRAM, 0);

  if (desc_data < 0) {   // handle error
    perror("cannot create socket\n");
    exit(-1);
  }

  setsockopt(desc_data, SOL_SOCKET, SO_REUSEADDR, &valid, sizeof(valid));

  if (bind(desc_data, (struct sockaddr*)com, alen) == -1) {
    perror("Bind socket fail\n");
    close(desc_data);
    exit(-1);
  }

  fd_set desc_set;
  FD_ZERO(&desc_set);
  FD_SET(desc_data, &desc_set);
  select(20, &desc_set, NULL, NULL, NULL); //attention, les desc 1 et 2 sont les entrées sorties standard

  char buffer[MSGSIZE + SEGSIZE]; //buffer reception et emission
  char buffer_ack[ACKSIZE];

  recvfrom(desc_data, &buffer, sizeof(buffer), 0, (struct sockaddr *)com, &alen); //reception du nom de fichier a envoyer

  FILE *f = fopen(buffer, "rb"); //ouverture fichier en lecture
  if(!f) {  //si le ficher n'existe pas : fermeture thread et client
    printf("%s inexistant \n", buffer);
    char *end_msg = "FIN";
    sendto(desc_data, end_msg, sizeof(end_msg), 0, (struct sockaddr *)com, alen);
    printf("fermeture THREAD \n");
    pthread_exit(NULL);
  }
  printf("%s ouvert\n", buffer);
  fseek(f, 0, SEEK_END); //pointeur a la fin du fichier
  int file_size = (int)ftell(f);  //recuperation de la taille
  int nb_total_seg = (int) ( (file_size - (file_size%MSGSIZE) + MSGSIZE)/MSGSIZE );
  rewind(f); //remise du pointeur au debut du fichier
  printf("taille du fichier a envoyer : %d bits\n", file_size);
  printf("nombre total de segment a enovoyer: %d\n", nb_total_seg);


  printf("start sending\n");
  printf("---------------\n\n");
  struct timeval time_init;
  double rtt = RTT_INIT; //RTT initialise a une valeur arbitraire
  double srtt = rtt; //idem RTT
  int iter=1;
  int x, i;
  int cwnd = CWND_ESTIM;
  int ack_tab[1+CWND_MAX];
  ack_tab[0]=1;
  int seg_rcv=0; //segments recu ET ack par le client
  int num_seg = 0;
  int resend;

  // if(nb_total_seg>NUM_SEG_ESTIM*20){
  if(nb_total_seg>NUM_SEG_ESTIM){
    do { //boucle d'envoi des premiers segment : estimation RTT
    printf("\nAttente : %d   CWND : %d\n", seg_rcv, cwnd);

      if(ack_tab[0]==1) { //si on a recu tous les ack de la fenetre precedente
        for(i = 0; i < (1+cwnd); i++) { ack_tab[i]=0; } //RAZ du tableau des ACK (de 0 a taille fenetre)

          for(x = 0; x<cwnd; x++) {
            num_seg++;
            sending(buffer, num_seg, 0, f, desc_data, com, &time_init, alen, cwnd);
          }

        int timeout = (int) (2*srtt*cwnd);
        int a = receive(buffer_ack, ack_tab, seg_rcv, 0, desc_data, com, alen, &time_init, timeout, cwnd);
        if(a != -1) {
          rtt = a/cwnd;
          srtt = srtt_next(srtt, rtt);
        }

      } else { //si on n'a pas recu tous les ack de la fenetre precedente
        for(i=1; i<=cwnd; i++) {
          if(ack_tab[i]==0) {
            sending(buffer, num_seg-cwnd+i, i, f, desc_data, com, &time_init, alen, cwnd);
          }
        }
        int timeout = (int) (2*srtt*cwnd);
        receive(buffer_ack, ack_tab, seg_rcv, 0, desc_data, com, alen, &time_init, timeout, cwnd);
      }

      if(ack_tab[0]==1) {
        seg_rcv+=cwnd;
        printf("RTT : %f || SRTT : %f\n", rtt, srtt);
      }
      printf("RTT : %f || SRTT : %f\n", rtt, srtt);
    } while( seg_rcv<NUM_SEG_ESTIM && iter == 1); //on recommence l'envoi si on n'a pas recu l'ACK ou si il est faux
  }
  
  // exit(1);

  //envoi de la suite du fichier :
  cwnd = CWND_INIT;
  while (iter) {

    do { //boucle d'envoi d'un segment
    printf("\nAttente : %d   CWND : %d\n", seg_rcv, cwnd);

    if(ack_tab[0]==1) {


      if( seg_rcv >= (nb_total_seg - cwnd)) { //gestion fin envoi
        cwnd = (nb_total_seg-seg_rcv - 1);
        //  printf("__________________cwnd %d, nb total seg %d, seg rcv %d\n", cwnd, nb_total_seg, seg_rcv);

        if( (nb_total_seg - seg_rcv) == 1) { //envoi dernier segment
          printf("envoi dernier segment\n" );

          num_seg++;
          memset(buffer, 0, MSGSIZE + SEGSIZE);
          init_seg(num_seg, buffer);
          int size = fread((&buffer[SEGSIZE]), (size_t)1, (size_t)MSGSIZE, f);


          char end_buffer[SEGSIZE + size];
          for(i=0; i<SEGSIZE + size; i++){
            end_buffer[i] = buffer[i];
          }
          for(i=0; i<10; i++) { //spam pour ne pas avoir a renvoyer
            if (sendto(desc_data, &end_buffer, sizeof(end_buffer), 0, (struct sockaddr *)com, alen) == -1) {
              int errsv = errno;
              printf("last sending failed\n");
              printf("%d\n", errsv);
            }
          }
          printf("taille dernier segment = %d\n", size);

          //message FIN
          iter = 0;

          printf("taille fichier envoye %d\n", file_size);
          printf("envoi message de fin d'envoi\n");
          int j;
          usleep(10);
          for(j=0; j<10; j++) {
            char *end_msg = "FIN";
            if (sendto(desc_data, end_msg, sizeof(end_msg), 0, (struct sockaddr *)com, alen) == -1) {
              int errsv = errno;
              printf("last sending failed\n");
              printf("%d\n", errsv);
            }
          }
        }
      }
      if(iter) {
        resend = 0;
        for(i = 0; i < (1+cwnd); i++) { ack_tab[i]=0; } //RAZ du tableau des ACK (de 0 a taille fenetre)

          for(x = 0; x<cwnd; x++) {
            num_seg++;
            sending(buffer, num_seg, 0, f, desc_data, com, &time_init, alen, cwnd);
          }

        int timeout = (int) (TIMEOUT*srtt*cwnd);
        receive(buffer_ack, ack_tab, seg_rcv, resend, desc_data, com, alen, &time_init, timeout, cwnd);
      }
    } else {
      resend++;
      for(i=1; i<=cwnd; i++) {
        if(ack_tab[i]==0) {
          sending(buffer, num_seg-cwnd+i, i, f, desc_data, com, &time_init, alen, cwnd);
        }
      }
      int timeout = (int) (TIMEOUT*srtt*cwnd);
      receive(buffer_ack, ack_tab, seg_rcv, resend, desc_data, com, alen, &time_init, timeout, cwnd);
    }

    if(ack_tab[0]==1) {
      seg_rcv+=cwnd;
    }

  } while(iter==1); //on recommence l'envoi si on n'a pas recu l'ACK ou si il est faux

  }
  printf("RTT : %f || SRTT : %f\n", rtt, srtt);
  fclose(f);
  printf("fermeture THREAD \n\n");
  pthread_exit(NULL);
}
