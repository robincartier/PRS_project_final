#define MSGSIZE 1478
#define ACKSIZE 9 //"ACK000000"
#define SEGSIZE 6 //"000000"
#define ALPHA 0.6
#define CWND_ESTIM 20
#define NUM_SEG_ESTIM 100
#define RTT_INIT 3000.0 //en microsecondes
#define TIMEOUT 1 //coef : cwnd * TIMEOUT * SRTT = timeout
#define WND_MAX 50
#define CWND_INIT 50
#define CWND_MAX 50
