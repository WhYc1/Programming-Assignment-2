#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h> // Required for memcpy

#include "emulator.h"
#include "sr.h"

/* ******************************************************************
Selective Repeat protocol. Adapted from J.F.Kurose
and the provided Go-Back-N implementation.

Network properties:
- one way network delay averages five time units (longer if there
  are other messages in the channel for GBN), but can be larger
- packets can be corrupted (either the header or the data portion)
  or lost, according to user-defined probabilities
- packets will be delivered in the order in which they were sent
  (although some can be lost).
**********************************************************************/

#define RTT 16.0 /* round trip time. MUST BE SET TO 16.0 when submitting assignment */
#define WINDOWSIZE 6 /* the maximum number of buffered unacked packet
                        MUST BE SET TO 6 when submitting assignment */
#define SEQSPACE 14 /* the min sequence space for SR must be at least windowsize * 2. Using 2*WINDOWSIZE + 2 or more is safer.*/
#define NOTINUSE (-1) /* used to fill header fields that are not being used*/
/* A and B are defined in emulator.h */
/* #define A 0 // Sender side */
/* #define B 1 // Receiver side */

/* generic procedure to compute the checksum of a packet. Used by both sender and receiver
   the simulator will overwrite part of your packet with 'z's. It will not overwrite your
   original checksum. This procedure must generate a different checksum to the original if
   the packet is corrupted.
*/
int ComputeChecksum(struct pkt packet)
{
  int checksum = 0;
  int i;

  checksum = packet.seqnum;
  checksum += packet.acknum;
  for ( i=0; i<20; i++ )
    checksum += (int)(packet.payload[i]);

  return checksum;
}

bool IsCorrupted(struct pkt packet)
{
  if (packet.checksum == ComputeChecksum(packet))
    return (false);
  else
    return (true);
}


/********* Sender (A) variables and functions ************/

static struct pkt A_packet_buffer[SEQSPACE]; /* array for storing packets waiting for ACK */
static bool A_packet_sent[SEQSPACE]; /* To track if a packet has been sent */
static bool A_packet_acked[SEQSPACE]; /* To track if a packet has been acknowledged */

static struct msg A_message_buffer[1000]; /* Buffer for messages from Layer 5 */
static int A_message_buffer_start; /* Start index of the message buffer */
static int A_message_buffer_end; /* End index of the message buffer */
static int A_message_buffer_count; /* Number of messages in the buffer */


static int A_send_base; /* Base of the sender window (sequence number) */
static int A_nextseqnum; /* The next sequence number to be used by the sender */

/* Helper function to send the next available packet from the message buffer */
// void A_send_next_packet() is added later

/* called from layer 5 (application layer), passed the message to be sent to other side*/
void A_output(struct msg message)
{
    // Initial implementation based on GBN, will be modified later
    struct pkt sendpkt;
    int i;

    /* if not blocked waiting on ACK */
    if ( /* Placeholder for SR window check */ 1 /* windowcount < WINDOWSIZE */ ) { // This check will be updated for SR
        if (TRACE > 1)
            printf("----A: New message arrives, send window is not full, send new messge to layer3!\n");

        /* create packet */
        sendpkt.seqnum = A_nextseqnum; // This will be updated later
        sendpkt.acknum = NOTINUSE;
        for ( i=0; i<20 ; i++ )
            sendpkt.payload[i] = message.data[i];
        sendpkt.checksum = ComputeChecksum(sendpkt);

        /* put packet in window buffer - SR buffers differently */
        // buffer[windowlast] = sendpkt; // GBN buffer, will be updated

        /* send out packet */
        if (TRACE > 0)
            printf("Sending packet %d to layer 3\n", sendpkt.seqnum);
        tolayer3 (A, sendpkt);

        /* start timer if first packet in window - timer logic changes for SR */
        // if (windowcount == 1) starttimer(A,RTT); // GBN timer, will be updated

        /* get next sequence number, wrap back to 0 - This logic is mostly ok but needs adjustment with buffering */
        A_nextseqnum = (A_nextseqnum + 1) % SEQSPACE; // This will be updated with buffering
    }
    /* if blocked,  window is full */
    else {
        if (TRACE > 0)
            printf("----A: New message arrives, send window is full\n");
        window_full++;
    }
}


/* called from layer 3, when a packet arrives for layer 4
   In this practical this will always be an ACK as B never sends data.
*/
void A_input(struct pkt packet)
{
    // GBN implementation, will be completely replaced for SR
    int ackcount = 0;
    int i;

    /* if received ACK is not corrupted */
    if (!IsCorrupted(packet)) {
        if (TRACE > 0)
            printf("----A: uncorrupted ACK %d is received\n",packet.acknum);
        total_ACKs_received++;

        /* check if new ACK or duplicate */
        // GBN ACK processing, will be replaced for SR
        if ( /* Placeholder for SR ACK check */ 1 /* windowcount != 0 */) {
             // int seqfirst = buffer[windowfirst].seqnum; // GBN buffer, will be updated
             // int seqlast = buffer[windowlast].seqnum;   // GBN buffer, will be updated
             /* check case when seqnum has and hasn't wrapped */
             // if (((seqfirst <= seqlast) && (packet.acknum >= seqfirst && packet.acknum <= seqlast)) ||
             //     ((seqfirst > seqlast) && (packet.acknum >= seqfirst || packet.acknum <= seqlast))) {

                 /* packet is a new ACK */
                 if (TRACE > 0)
                     printf("----A: ACK %d is not a duplicate\n",packet.acknum);
                 new_ACKs++;

                 /* cumulative acknowledgement - determine how many packets are ACKed - GBN specific */
                 // if (packet.acknum >= seqfirst) ackcount = packet.acknum + 1 - seqfirst;
                 // else ackcount = SEQSPACE - seqfirst + packet.acknum;

                 /* slide window by the number of packets ACKed - GBN specific */
                 // windowfirst = (windowfirst + ackcount) % WINDOWSIZE;

                 /* delete the acked packets from window buffer - GBN specific */
                 // for (i=0; i<ackcount; i++) windowcount--;

                 /* start timer again if there are still more unacked packets in window - GBN specific */
                 stoptimer(A); // Timer logic will change for SR
                 // if (windowcount > 0) starttimer(A, RTT);

             // }
         }
        else
            if (TRACE > 0)
                printf ("----A: duplicate ACK received, do nothing!\n");
    }
    else
        if (TRACE > 0)
            printf ("----A: corrupted ACK is received, do nothing!\n");
}

/* called when A's timer goes off*/
void A_timerinterrupt(void)
{
    // GBN implementation, will be completely replaced for SR
    int i;

    if (TRACE > 0)
        printf("----A: time out,resend packets!\n");

    // GBN retransmission logic, will be replaced for SR
    // for(i=0; i<windowcount; i++) {
    //
    //     if (TRACE > 0)
    //         printf ("---A: resending packet %d\n", (buffer[(windowfirst+i) % WINDOWSIZE]).seqnum);
    //
    //     tolayer3(A,buffer[(windowfirst+i) % WINDOWSIZE]);
    //     packets_resent++;
    //     if (i==0) starttimer(A,RTT);
    // }

}



/* the following routine will be called once (only) before any other */
/* entity A routines are called. You can use it to do any initialization*/
void A_init(void)
{
    // Initial GBN initialization, will be updated for SR
    int i;
    A_send_base = 0;
    A_nextseqnum = 0;
    for(i = 0; i < SEQSPACE; i++){ // SEQSPACE is larger for SR
        A_packet_sent[i] = false; // Added for SR
        A_packet_acked[i] = false; // Added for SR
    }
    A_message_buffer_start = 0; // Added for SR
    A_message_buffer_end = 0;   // Added for SR
    A_message_buffer_count = 0; // Added for SR

    /* initialise A's window, buffer and sequence number - GBN specific, some variables reused differently in SR */
    // A_nextseqnum = 0;  /* A starts with seq num 0, do not change this */
    // windowfirst = 0;
    // windowlast = -1;   /* windowlast is where the last packet sent is stored.
    // 		     new packets are placed in winlast + 1
    // 		     so initially this is set to -1
    // 		   */
    // windowcount = 0;
}



/********* Receiver (B)  variables and procedures ************/

static int B_expectedseqnum; /* the sequence number expected next by the receiver */
// static int B_nextseqnum;   /* the sequence number for the next packets sent by B - not needed for simplex SR */

static struct pkt B_packet_buffer[SEQSPACE]; /* Buffer for out-of-order packets - Added for SR */
static bool B_packet_buffered[SEQSPACE]; /* To track if a packet is buffered in the receiver buffer - Added for SR */


/* called from layer 3, when a packet arrives for layer 4 at B*/
void B_input(struct pkt packet)
{
    // GBN implementation, will be completely replaced for SR
    struct pkt sendpkt;
    int i;

    /* if not corrupted and received packet is in order */
    if  ( (!IsCorrupted(packet))  && (packet.seqnum == expectedseqnum) ) { // Sequence number check is GBN specific
        if (TRACE > 0)
            printf("----B: packet %d is correctly received, send ACK!\n",packet.seqnum);
        packets_received++;

        /* deliver to receiving application */
        tolayer5(B, packet.payload);

        /* send an ACK for the received packet */
        sendpkt.acknum = expectedseqnum; // GBN ACK logic, will be updated for SR

        /* update state variables */
        expectedseqnum = (expectedseqnum + 1) % SEQSPACE; // GBN state update, will be updated for SR
    }
    else {
        /* packet is corrupted or out of order resend last ACK - GBN specific */
        if (TRACE > 0)
            printf("----B: packet corrupted or not expected sequence number, resend ACK!\n");
        // if (expectedseqnum == 0) sendpkt.acknum = SEQSPACE - 1;
        // else sendpkt.acknum = expectedseqnum - 1;
        // ACK logic will be updated for SR
    }

    /* create packet - ACK packet creation is mostly fine but seqnum handling differs */
    sendpkt.seqnum = 0; // B_nextseqnum; // Not needed for simplex SR data. Use 0 or NOTINUSE for ACKs.
    // B_nextseqnum = (B_nextseqnum + 1) % 2; // Not needed for simplex SR

    /* we don't have any data to send.  fill payload with 0's */
    for ( i=0; i<20 ; i++ )
        sendpkt.payload[i] = '0';

    /* computer checksum */
    sendpkt.checksum = ComputeChecksum(sendpkt);

    /* send out packet */
    tolayer3 (B, sendpkt);
}

/* the following routine will be called once (only) before any other */
/* entity B routines are called. You can use it to do any initialization */
void B_init(void)
{
    // Initial GBN initialization, will be updated for SR
    int i;
    B_expectedseqnum = 0; // This variable is used in SR
    // B_nextseqnum = 1; // Not needed for simplex SR

    for(i = 0; i < SEQSPACE; i++){ // SEQSPACE is larger for SR
        B_packet_buffered[i] = false; // Added for SR
        // Initialize packet buffer entries (optional but good practice)
        // memset(&B_packet_buffer[i], 0, sizeof(struct pkt)); // Added for SR, needs include <string.h>
    }
}

/******************************************************************************
 * The following functions need be completed only for bi-directional messages *
 *****************************************************************************/

/* Note that with simplex transfer from a-to-B, there is no B_output() */
void B_output(struct msg message)
{
}

/* called when B's timer goes off*/
void B_timerinterrupt(void)
{
}