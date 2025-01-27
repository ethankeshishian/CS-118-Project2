#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <fcntl.h>
#include <netdb.h> 

// =====================================

#define RTO 500000 /* timeout in microseconds */
#define HDR_SIZE 12 /* header size*/
#define PKT_SIZE 524 /* total packet size */
#define PAYLOAD_SIZE 512 /* PKT_SIZE - HDR_SIZE */
#define WND_SIZE 10 /* window size*/
#define MAX_SEQN 25601 /* number of sequence numbers [0-25600] */
#define FIN_WAIT 2 /* seconds to wait after receiving FIN*/

// Packet Structure: Described in Section 2.1.1 of the spec. DO NOT CHANGE!
struct packet {
    unsigned short seqnum;
    unsigned short acknum;
    char syn;
    char fin;
    char ack;
    char dupack;
    unsigned int length;
    char payload[PAYLOAD_SIZE];
};

// Printing Functions: Call them on receiving/sending/packet timeout according
// Section 2.6 of the spec. The content is already conformant with the spec,
// no need to change. Only call them at correct times.
void printRecv(struct packet* pkt) {
    printf("RECV %d %d%s%s%s\n", pkt->seqnum, pkt->acknum, pkt->syn ? " SYN": "", pkt->fin ? " FIN": "", (pkt->ack || pkt->dupack) ? " ACK": "");
}

void printSend(struct packet* pkt, int resend) {
    if (resend)
        printf("RESEND %d %d%s%s%s\n", pkt->seqnum, pkt->acknum, pkt->syn ? " SYN": "", pkt->fin ? " FIN": "", pkt->ack ? " ACK": "");
    else
        printf("SEND %d %d%s%s%s%s\n", pkt->seqnum, pkt->acknum, pkt->syn ? " SYN": "", pkt->fin ? " FIN": "", pkt->ack ? " ACK": "", pkt->dupack ? " DUP-ACK": "");
}

void printTimeout(struct packet* pkt) {
    printf("TIMEOUT %d\n", pkt->seqnum);
}

// Building a packet by filling the header and contents.
// This function is provided to you and you can use it directly
void buildPkt(struct packet* pkt, unsigned short seqnum, unsigned short acknum, char syn, char fin, char ack, char dupack, unsigned int length, const char* payload) {
    pkt->seqnum = seqnum;
    pkt->acknum = acknum;
    pkt->syn = syn;
    pkt->fin = fin;
    pkt->ack = ack;
    pkt->dupack = dupack;
    pkt->length = length;
    memcpy(pkt->payload, payload, length);
}

// =====================================

double setTimer() {
    struct timeval e;
    gettimeofday(&e, NULL);
    return (double) e.tv_sec + (double) e.tv_usec/1000000 + (double) RTO/1000000;
}

double setFinTimer() {
    struct timeval e;
    gettimeofday(&e, NULL);
    return (double) e.tv_sec + (double) e.tv_usec/1000000 + (double) FIN_WAIT;
}

int isTimeout(double end) {
    struct timeval s;
    gettimeofday(&s, NULL);
    double start = (double) s.tv_sec + (double) s.tv_usec/1000000;
    return ((end - start) < 0.0);
}

// =====================================

int main (int argc, char *argv[])
{
    if (argc != 4) {
        perror("ERROR: incorrect number of arguments\n");
        exit(1);
    }

    struct in_addr servIP;
    if (inet_aton(argv[1], &servIP) == 0) {
        struct hostent* host_entry; 
        host_entry = gethostbyname(argv[1]); 
        if (host_entry == NULL) {
            perror("ERROR: IP address not in standard dot notation\n");
            exit(1);
        }
        servIP = *((struct in_addr*) host_entry->h_addr_list[0]);
    }

    unsigned int servPort = atoi(argv[2]);

    FILE* fp = fopen(argv[3], "r");
    if (fp == NULL) {
        perror("ERROR: File not found\n");
        exit(1);
    }

    // =====================================
    // Socket Setup

    int sockfd;
    struct sockaddr_in servaddr;
    
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    servaddr.sin_family = AF_INET;
    servaddr.sin_addr = servIP;
    servaddr.sin_port = htons(servPort);
    memset(servaddr.sin_zero, '\0', sizeof(servaddr.sin_zero));

    int servaddrlen = sizeof(servaddr);

    // NOTE: We set the socket as non-blocking so that we can poll it until
    //       timeout instead of getting stuck. This way is not particularly
    //       efficient in real programs but considered acceptable in this
    //       project.
    //       Optionally, you could also consider adding a timeout to the socket
    //       using setsockopt with SO_RCVTIMEO instead.
    fcntl(sockfd, F_SETFL, O_NONBLOCK);

    // =====================================
    // Establish Connection: This procedure is provided to you directly and is
    // already working.
    // Note: The third step (ACK) in three way handshake is sent along with the
    // first piece of along file data thus is further below

    struct packet synpkt, synackpkt;

    unsigned short seqNum = rand() % MAX_SEQN;
    buildPkt(&synpkt, seqNum, 0, 1, 0, 0, 0, 0, NULL);

    printSend(&synpkt, 0);
    sendto(sockfd, &synpkt, PKT_SIZE, 0, (struct sockaddr*) &servaddr, servaddrlen);
    double timer = setTimer();
    int n;

    while (1) {
        while (1) {
            n = recvfrom(sockfd, &synackpkt, PKT_SIZE, 0, (struct sockaddr *) &servaddr, (socklen_t *) &servaddrlen);

            if (n > 0)
                break;
            else if (isTimeout(timer)) {
                printTimeout(&synpkt);
                printSend(&synpkt, 1);
                sendto(sockfd, &synpkt, PKT_SIZE, 0, (struct sockaddr*) &servaddr, servaddrlen);
                timer = setTimer();
            }
        }

        printRecv(&synackpkt);
        if ((synackpkt.ack || synackpkt.dupack) && synackpkt.syn && synackpkt.acknum == (seqNum + 1) % MAX_SEQN) {
            seqNum = synackpkt.acknum;
            break;
        }
    }

    // =====================================
    // FILE READING VARIABLES
    
    char buf[PAYLOAD_SIZE];
    size_t m;

    // =====================================
    // CIRCULAR BUFFER VARIABLES

    struct packet ackpkt;
    struct packet pkts[WND_SIZE];
    int s = 0;
    int e = 0;
    int full = 0;

    // =====================================
    // Send First Packet (ACK containing payload)
    
    // COMMENTED OUT FOR CONTROL OVER FIRST PACKET 
    // m = fread(buf, 1, PAYLOAD_SIZE, fp);

    // buildPkt(&pkts[0], seqNum, (synackpkt.seqnum + 1) % MAX_SEQN, 0, 0, 1, 0, m, buf);
    // printSend(&pkts[0], 0);
    // sendto(sockfd, &pkts[0], PKT_SIZE, 0, (struct sockaddr*) &servaddr, servaddrlen);
    // timer = setTimer();
    // buildPkt(&pkts[0], seqNum, (synackpkt.seqnum + 1) % MAX_SEQN, 0, 0, 0, 1, m, buf);

    // e = 1;

    // =====================================
    // *** TODO: Implement the rest of reliable transfer in the client ***
    // Implement GBN for basic requirement or Selective Repeat to receive bonus

    // Note: the following code is not the complete logic. It only sends a
    //       single data packet, and then tears down the connection without
    //       handling data loss.
    //       Only for demo purpose. DO NOT USE IT in your final submission
    
    int start = 1; // we're handling the first packet here instead of above
    int window_filling = 0;
    int done_transmitting = 0;
    int fin_ack_found = 0;
    unsigned short fin_ack = 0;
    int prev_m = 0;
    unsigned short greatest_acked = synackpkt.acknum;
    // printf("synack %d\n", synackpkt.acknum);
    // printf("greatest before %d\n", greatest_acked);
    unsigned short prev_greatest = 0;
    int resend = 0;
    int timeout_index = -1;

    while (1) { // break out of while after full file accepted
        if (resend) {
            for (short i = 0; i < window_filling; i++) {
                // printf("resending i: %d | seq: %d\n", i, pkts[i].seqnum);
                int resend_window = (greatest_acked + (WND_SIZE * PAYLOAD_SIZE)) % MAX_SEQN;
                // printf("resend window: %d | greatest_acked: %d\n", resend_window, greatest_acked);
                if (((pkts[i].seqnum > greatest_acked && pkts[i].seqnum <= resend_window) ||
                (greatest_acked > resend_window && (pkts[i].seqnum < resend_window || pkts[i].seqnum > greatest_acked)))
                 && pkts[i].syn != 1){
                    timer = setTimer();
                    timeout_index = i;
                    printSend(&pkts[i], 1);
                    sendto(sockfd, &pkts[i], PKT_SIZE, 0, (struct sockaddr*) &servaddr, servaddrlen);
                }
            }
            resend = 0;
            continue;
        }        
        else if (isTimeout(timer)){
            resend = 1;
            printTimeout(&pkts[timeout_index]);
            continue;
        }
        else if (!start){
            n = recvfrom(sockfd, &ackpkt, PKT_SIZE, 0, (struct sockaddr *) &servaddr, (socklen_t *) &servaddrlen);
            // printf("%d", n);
        }

        if (done_transmitting && (fin_ack == greatest_acked)) {
            // printf("fin_ack %d | greatest_acked %d\n", fin_ack, greatest_acked);
            fin_ack_found = 1;
            goto fin;
        }

        if (n > 0) {
            if (!start && (greatest_acked < ackpkt.acknum || ackpkt.acknum < (greatest_acked + WND_SIZE * PAYLOAD_SIZE) % MAX_SEQN)){
                prev_greatest = greatest_acked;
                greatest_acked = ackpkt.acknum;
            }
            // printf("n %d, isSynAck %c, start %d, greatest %d\n", n, ackpkt.syn, start, greatest_acked);
            if (!start) {
                printRecv(&ackpkt);
                // fin_ack = ackpkt.seqnum;
                unsigned short prevack = ackpkt.acknum;
                unsigned short prevseq = ackpkt.seqnum;
            }
            // generate & send up to ten packets
            if (window_filling < WND_SIZE) {
                // printf("synack %d\n", synackpkt.acknum);
                // printf("greatest before %d\n", greatest_acked);
                for (short i = window_filling; i < 10; i++) {
                    m = fread(buf, 1, PAYLOAD_SIZE, fp);
                    // printf("m %d | firstten %d\n", m, window_filling);
                    if (m <= 0) {
                        // printf("fin_ack %d | greatest_acked %d | seqNum %d | prev_m %d\n", fin_ack, greatest_acked, seqNum, prev_m);
                        // fin_ack = (seqNum + prev_m) % MAX_SEQN; // calculate the final ack we're looking for
                        done_transmitting = 1;
                        goto fin; // the only way to break out of a double loop immediately
                    } else {
                        prev_m = m; // we keep this counter so that we can calculate the final ack
                        if (start) {
                            buildPkt(&pkts[i], seqNum, (synackpkt.seqnum + 1) % MAX_SEQN, 0, 0, 1, 0, m, buf);
                            printSend(&pkts[i], 0);
                            sendto(sockfd, &pkts[0], PKT_SIZE, 0, (struct sockaddr*) &servaddr, servaddrlen);
                            timer = setTimer();
                            timeout_index = i;
                            buildPkt(&pkts[i], seqNum, (synackpkt.seqnum + 1) % MAX_SEQN, 0, 0, 0, 1, m, buf);
                            start = 0;
                            fin_ack = pkts[i].seqnum;
                            greatest_acked = pkts[i].seqnum - pkts[i].length;
                        } else {
                            seqNum = seqNum + m;
                            // Subsequent packets don't need an ACK per spec (set to 0)
                            timer = setTimer();
                            timeout_index = i;
                            buildPkt(&pkts[i], seqNum % MAX_SEQN, 0, 0, 0, 0, 0, m, buf);
                            printSend(&pkts[i], 0);
                            sendto(sockfd, &pkts[i], PKT_SIZE, 0, (struct sockaddr*) &servaddr, servaddrlen);
                            fin_ack = pkts[i].seqnum;
                        }
                    
                    }
                    window_filling++;
                }
                // first_ten = 0; // after the first ten, we send each subsequent one individually with received acks
            } else {
                for (short i = 0; i < window_filling; i++) {
                    // these are the individual ones
                    if ((pkts[i].seqnum <= greatest_acked && pkts[i].seqnum >= prev_greatest) ||
                        (prev_greatest > greatest_acked && (pkts[i].seqnum <= greatest_acked || pkts[i].seqnum >= prev_greatest))){
                        m = fread(buf, 1, PAYLOAD_SIZE, fp);
                        // printf("m %d | firstten %d\n", m, window_filling);
                        if (m <= 0) {
                            // same logic as above
                            // fin_ack = (seqNum + prev_m) % MAX_SEQN;
                            done_transmitting = 1;
                            goto fin;
                        }
                        prev_m = m;
                        seqNum = seqNum + m;
                        // Subsequent packets don't need an ACK per spec (set to 0)
                        timer = setTimer();
                        timeout_index = i;
                        buildPkt(&pkts[i], seqNum % MAX_SEQN, 0, 0, 0, 0, 0, m, buf);
                        printSend(&pkts[i], 0);
                        sendto(sockfd, &pkts[i], PKT_SIZE, 0, (struct sockaddr*) &servaddr, servaddrlen);
                        fin_ack = pkts[i].seqnum;
                    }
                }
            }
            fin: { 
                // if the fin ack is not found yet, don't break
                if (m <= 0) { 
                    if (fin_ack_found)
                        break; 
            } }
        }
    }

    // *** End of your client implementation ***
    fclose(fp);
    // printf("milestone: end of client implementation\n"); // REMOVE LATER

    // =====================================
    // Connection Teardown: This procedure is provided to you directly and is
    // already working.

    struct packet finpkt, recvpkt;
    buildPkt(&finpkt, ackpkt.acknum, 0, 0, 1, 0, 0, 0, NULL);
    buildPkt(&ackpkt, (ackpkt.acknum + 1) % MAX_SEQN, (ackpkt.seqnum + 1) % MAX_SEQN, 0, 0, 1, 0, 0, NULL);

    printSend(&finpkt, 0);
    sendto(sockfd, &finpkt, PKT_SIZE, 0, (struct sockaddr*) &servaddr, servaddrlen);
    timer = setTimer();
    int timerOn = 1;

    double finTimer;
    int finTimerOn = 0;

    while (1) {
        while (1) {
            n = recvfrom(sockfd, &recvpkt, PKT_SIZE, 0, (struct sockaddr *) &servaddr, (socklen_t *) &servaddrlen);

            if (n > 0)
                break;
            if (timerOn && isTimeout(timer)) {
                printTimeout(&finpkt);
                printSend(&finpkt, 1);
                if (finTimerOn)
                    timerOn = 0;
                else
                    sendto(sockfd, &finpkt, PKT_SIZE, 0, (struct sockaddr*) &servaddr, servaddrlen);
                timer = setTimer();
            }
            if (finTimerOn && isTimeout(finTimer)) {
                close(sockfd);
                if (! timerOn)
                    exit(0);
            }
        }
        printRecv(&recvpkt);
        if ((recvpkt.ack || recvpkt.dupack) && recvpkt.acknum == (finpkt.seqnum + 1) % MAX_SEQN) {
            timerOn = 0;
        }
        else if (recvpkt.fin && (recvpkt.seqnum + 1) % MAX_SEQN == ackpkt.acknum) {
            printSend(&ackpkt, 0);
            sendto(sockfd, &ackpkt, PKT_SIZE, 0, (struct sockaddr*) &servaddr, servaddrlen);
            finTimer = setFinTimer();
            finTimerOn = 1;
            buildPkt(&ackpkt, ackpkt.seqnum, ackpkt.acknum, 0, 0, 0, 1, 0, NULL);
        }
    }
}
