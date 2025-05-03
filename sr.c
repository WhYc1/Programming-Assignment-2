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
void A_send_next_packet() {
    struct pkt sendpkt;
    int i;

    /* Check if there are messages in the buffer and space in the sender window */
    while (A_message_buffer_count > 0 && A_nextseqnum < A_send_base + WINDOWSIZE) {

        if (TRACE > 1)
            printf("----A: Sending packet for buffered message with seq num %d\n", A_nextseqnum);

        /* Create packet from the oldest buffered message */
        sendpkt.seqnum = A_nextseqnum;
        sendpkt.acknum = NOTINUSE; /* ACK field not used by sender data packets */
        for ( i=0; i<20 ; i++ )
            sendpkt.payload[i] = A_message_buffer[A_message_buffer_start].data[i];
        sendpkt.checksum = ComputeChecksum(sendpkt);

        /* Put packet in packet buffer */
        A_packet_buffer[A_nextseqnum % SEQSPACE] = sendpkt;
        A_packet_sent[A_nextseqnum % SEQSPACE] = true;
        A_packet_acked[A_nextseqnum % SEQSPACE] = false; /* Mark as not yet acknowledged */

        /* Send out packet */
        if (TRACE > 0)
            printf("Sending packet %d to layer 3\n", sendpkt.seqnum);
        tolayer3 (A, sendpkt);

        /* Start timer if this is the first unacked packet in the window */
        if (A_send_base == A_nextseqnum) { /* Check if this is the first packet in the current window */
            starttimer(A, RTT);
        }

        /* Move to the next message in the buffer */
        A_message_buffer_start = (A_message_buffer_start + 1) % 1000;
        A_message_buffer_count--;

        /* Get next sequence number */
        A_nextseqnum++;
    }
}


/* called from layer 5 (application layer), passed the message to be sent to other side*/
void A_output(struct msg message)
{
    /* Buffer the incoming message from Layer 5 */
    if (A_message_buffer_count < 1000) { /* Check if message buffer is not full */
        A_message_buffer[A_message_buffer_end] = message;
        A_message_buffer_end = (A_message_buffer_end + 1) % 1000;
        A_message_buffer_count++;

        if (TRACE > 1)
            printf("----A: Message buffered from layer 5. Buffer count: %d\n", A_message_buffer_count);

        /* Try to send packets from the buffer if there is space in the window */
        A_send_next_packet();

    } else {
        if (TRACE > 0)
            printf("----A: Message buffer is full, dropping message from layer 5\n");
        /* This case should ideally not happen with a large buffer, but included for completeness */
        window_full++; /* Use the provided statistic for dropped messages */
    }
}


/* called from layer 3, when a packet arrives for layer 4
   In this practical this will always be an ACK as B never sends data.
*/
void A_input(struct pkt packet)
{
    // Placeholder, will be implemented in next steps
}

/* called when A's timer goes off*/
void A_timerinterrupt(void)
{
    // Placeholder, will be implemented in next steps
}



/* the following routine will be called once (only) before any other */
/* entity A routines are called. You can use it to do any initialization*/
void A_init(void)
{
    int i;
    A_send_base = 0;
    A_nextseqnum = 0;
    for(i = 0; i < SEQSPACE; i++){
        A_packet_sent[i] = false;
        A_packet_acked[i] = false;
    }
    A_message_buffer_start = 0;
    A_message_buffer_end = 0;
    A_message_buffer_count = 0;
}



/********* Receiver (B)  variables and procedures ************/

static int B_expectedseqnum; /* The sequence number expected next by the receiver */
// static int B_nextseqnum;   /* the sequence number for the next packets sent by B - not needed for simplex SR */

static struct pkt B_packet_buffer[SEQSPACE]; /* Buffer for out-of-order packets - Added for SR */
static bool B_packet_buffered[SEQSPACE]; /* To track if a packet is buffered in the receiver buffer - Added for SR */


/* called from layer 3, when a packet arrives for layer 4 at B*/
void B_input(struct pkt packet)
{
    // Placeholder, will be implemented in next steps
    struct pkt sendpkt;
    int i;

    /* create ACK packet to send immediately */
    sendpkt.seqnum = 0; /* Sender doesn't care about this for ACKs */
    sendpkt.payload[0] = '0'; /* No data in ACK packet */
    for(i=1; i<20; i++) sendpkt.payload[i] = '0'; /* Fill the rest with 0s */

    // Rest of B_input logic will be implemented in next steps
}

/* the following routine will be called once (only) before any other */
/* entity B routines are called. You can use it to do any initialization*/
void B_init(void)
{
    // Placeholder, will be implemented in next steps
    int i;
    B_expectedseqnum = 0;
    for(i = 0; i < SEQSPACE; i++){
        B_packet_buffered[i] = false;
        // Initialize packet buffer entries (optional but good practice)
        // memset(&B_packet_buffer[i], 0, sizeof(struct pkt));
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
