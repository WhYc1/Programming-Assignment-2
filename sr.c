#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <float.h>
#include "emulator.h"
#include "gbn.h"

/* ******************************************************************
   Selective Repeat Protocol.  Adapted from J.F.Kurose
   ALTERNATING BIT AND GO-BACK-N NETWORK EMULATOR: VERSION 1.2

   Network properties:
   - one way network delay averages five time units (longer if there
     are other messages in the channel for GBN), but can be larger
   - packets can be corrupted (either the header or the data portion)
     or lost, according to user-defined probabilities
   - packets will be delivered in the order in which they were sent
     (although some can be lost).

   Modifications:
   - removed bidirectional GBN code and other code not used by prac.
   - fixed C style to adhere to current programming style
   - added SR implementation attempt
**********************************************************************/

#define RTT 16.0 /* round trip time. MUST BE SET TO 16.0 when submitting assignment */
#define WINDOWSIZE                                                                                                     \
    6 /* the maximum number of buffered unacked packet                                                                 \
         MUST BE SET TO 6 when submitting assignment */
#define SEQSPACE                                                                                                       \
    (2 * WINDOWSIZE)  /* the min sequence space for SR must be at least 2 *                                            \
                         windowsize */
#define NOTINUSE (-1) /* used to fill header fields that are not being used */

/* Helper function to find minimum of two doubles */
double min_double(double a, double b) { return (a < b) ? a : b; }

/* generic procedure to compute the checksum of a packet.  Used by both sender and receiver
   the simulator will overwrite part of your packet with 'z's.  It will not overwrite your
   original checksum.  This procedure must generate a different checksum to the original if
   the packet is corrupted.
*/
int ComputeChecksum(struct pkt packet) {
    int checksum = 0;
    int i;

    checksum = packet.seqnum;
    checksum += packet.acknum;
    for (i = 0; i < 20; i++) checksum += (int)(packet.payload[i]);

    return checksum;
}

bool IsCorrupted(struct pkt packet) {
    if (packet.checksum == ComputeChecksum(packet)) return (false);
    else
        return (true);
}

/********* Sender (A) variables and functions ************/

static struct pkt buffer[WINDOWSIZE]; /* array for storing packets waiting for ACK */
static int windowfirst, windowlast;   /* array indexes of the first/last packet awaiting ACK */
static int windowcount;               /* the number of packets currently awaiting an ACK */
static int A_nextseqnum;              /* the next sequence number to be used by the sender */
static int A_status[WINDOWSIZE];      /* Status of packets in the buffer */

#define AS_NONE 0 /* Slot is empty */
#define AS_SENT 1 /* Packet sent, timer running, waiting for ACK */
#define AS_RCVD 2 /* ACK received, but packet potentially not slided past yet */

/* called from layer 5 (application layer), passed the message to be sent to other side */
void A_output(struct msg message) {
    struct pkt sendpkt;
    int i;

    /* if not blocked waiting on ACK */
    if (windowcount < WINDOWSIZE) {
        if (TRACE > 1)
            printf("----A: New message arrives, send window is not full, send new messge to "
                   "layer3!\n");

        /* create packet */
        sendpkt.seqnum = A_nextseqnum;
        sendpkt.acknum = NOTINUSE;
        for (i = 0; i < 20; i++) sendpkt.payload[i] = message.data[i];
        sendpkt.checksum = ComputeChecksum(sendpkt);

        /* put packet in window buffer */
        windowlast = (windowlast + 1) % WINDOWSIZE;
        buffer[windowlast] = sendpkt;
        A_status[windowlast] = AS_SENT;
        windowcount++;

        /* send out packet */
        if (TRACE > 0) printf("Sending packet %d to layer 3\n", sendpkt.seqnum);
        tolayer3(A, sendpkt);

        /* Start timer only if it's the first packet in the window */
        if (windowcount == 1) { starttimer(A, RTT); }

        /* get next sequence number, wrap back to 0 */
        A_nextseqnum = (A_nextseqnum + 1) % SEQSPACE;
    }
    /* if blocked,  window is full */
    else {
        if (TRACE > 0) printf("----A: New message arrives, send window is full\n");
        window_full++;
    }
}

/* called from layer 3, when a packet arrives for layer 4
   In this practical this will always be an ACK as B never sends data.
*/
void A_input(struct pkt packet) {
    int i;
    bool ack_in_window = false;
    int ackidx = -1; /* Index in the buffer */
    int current_idx;

    /* if received ACK is not corrupted */
    if (!IsCorrupted(packet)) {
        if (TRACE > 0) printf("----A: uncorrupted ACK %d is received\n", packet.acknum);
        total_ACKs_received++;

        /* check if ACK is for a packet currently in the sender's window */
        if (windowcount > 0) {
            /* Check if acknum corresponds to any packet seqnum within the current window count */
            /* This check handles sequence number wrap-around implicitly by comparing with buffered */
            /* sequence numbers. */
            for (i = 0; i < windowcount; ++i) {
                current_idx = (windowfirst + i) % WINDOWSIZE;
                if (buffer[current_idx].seqnum == packet.acknum) {
                    ack_in_window = true;
                    ackidx = current_idx; /* Found index */
                    break;
                }
            }

            if (ack_in_window) {
                /* Check if we haven't already processed an ACK for this packet */
                if (A_status[ackidx] == AS_SENT) {

                    if (TRACE > 0)
                        printf("----A: ACK %d is not a duplicate\n", packet.acknum); /* Keep original print */
                    new_ACKs++;

                    /* Mark packet as received and stop its logical timer */
                    A_status[ackidx] = AS_RCVD;

                    /* Slide the window base (windowfirst) past all contiguously acknowledged */
                    /* packets */
                    while (windowcount > 0 && A_status[windowfirst] == AS_RCVD) {
                        A_status[windowfirst] = AS_NONE; /* Reset status for the buffer slot */
                        windowfirst = (windowfirst + 1) % WINDOWSIZE;
                        windowcount--;
                    }

                    /* Restart the single physical timer based on remaining packets */
                    stoptimer(A);
                    if (windowcount > 0) { starttimer(A, RTT); }

                } else {
                    /* Received ACK for a packet already marked as RCVD (or somehow not SENT). */
                    /* This can happen if ACKs are duplicated by the network. */
                    if (TRACE > 0) printf("----A: duplicate ACK received, do nothing!\n"); /* Keep original print */
                }
            } else {
                /* Could be an ACK for a packet already slid past, or an invalid ACK#. */
                if (TRACE > 0) printf("----A: duplicate ACK received, do nothing!\n"); /* Keep original print */
            }
        } else {
            /* Window is empty, any ACK received is technically a duplicate or old. */
            if (TRACE > 0) printf("----A: duplicate ACK received, do nothing!\n"); /* Keep original print */
        }
    } else {
        /* Corrupted ACK - Keep original print */
        if (TRACE > 0) printf("----A: corrupted ACK is received, do nothing!\n");
    }
}

/* called when A's timer goes off */
void A_timerinterrupt(void) {
    int k;
    int idx;
    bool resent_any = false;

    /* Identify the first packet whose timers have effectively expired */
    for (k = 0; k < windowcount; k++) {
        idx = (windowfirst + k) % WINDOWSIZE;
        if (k < windowcount && A_status[idx] == AS_SENT) {

            /* Check if this packet's timer expired */
            /* This packet timed out */
            if (!resent_any) {
                if (TRACE > 0) printf("----A: time out,resend packets!\n");
                resent_any = true;
            }
            if (TRACE > 0) printf("---A: resending packet %d\n", (buffer[idx]).seqnum);

            tolayer3(A, buffer[idx]);
            packets_resent++;
        }
        if (k == 0) starttimer(A, RTT);
        if (resent_any) break;
    }
}

/* the following routine will be called once (only) before any other */
/* entity A routines are called. You can use it to do any initialization */
void A_init(void) {
    int i;
    /* initialise A's window, buffer and sequence number */
    A_nextseqnum = 0; /* A starts with seq num 0, do not change this */
    windowfirst = 0;
    windowlast = -1; /* windowlast is where the last packet sent is stored.
                        new packets are placed in winlast + 1
                        so initially this is set to -1
                     */
    windowcount = 0;

    /* Initialize packet status and timers */
    for (i = 0; i < WINDOWSIZE; i++) { A_status[i] = AS_NONE; }
}

/********* Receiver (B)  variables and procedures ************/

static int expectedseqnum; /* SR: This is rcv_base, the start of the receive window */
static int B_nextseqnum;   /* SR: Sequence number for ACK packets sent by B (largely irrelevant in
                              simplex) */

static struct pkt B_buffer[WINDOWSIZE]; /* Buffer for out-of-order packets */
static int B_windowfirst;               /* Index in B_buffer corresponding to expectedseqnum (rcv_base) */
static int B_status[WINDOWSIZE];        /* Status of buffer slots */

#define BS_NONE 0     /* Slot is empty / Expected but not received */
#define BS_RECEIVED 1 /* Packet received and buffered, ACK sent */

/* called from layer 3, when a packet arrives for layer 4 at B*/
void B_input(struct pkt packet) {
    struct pkt sendpkt;
    int i;
    int rcv_base;
    int off;
    int idx;

    /* Calculate window boundaries */
    rcv_base = expectedseqnum;

    /* Process based on window check and corruption status */
    if (!IsCorrupted(packet)) {
        /* Packet is within the expected receive window [rcv_base, rcv_base+N-1] */
        if (TRACE > 0) printf("----B: packet %d is correctly received, send ACK!\n", packet.seqnum);

        /* --- Send ACK for the specific packet received --- */
        sendpkt.acknum = packet.seqnum;

        /* --- Buffer the packet if it hasn't been received before --- */
        off = (packet.seqnum - rcv_base + SEQSPACE) % SEQSPACE;
        idx = (B_windowfirst + off) % WINDOWSIZE;

        if (B_status[idx] == BS_NONE) {
            B_buffer[idx] = packet;
            B_status[idx] = BS_RECEIVED; /* Mark as received */

            /* --- Try to deliver contiguous packets starting from rcv_base --- */
            while (B_status[B_windowfirst] == BS_RECEIVED) {
                tolayer5(B, B_buffer[B_windowfirst].payload);
                packets_received++; /* Increment count *only* when delivered */

                /* Advance window: clear buffer slot, move windowfirst index, increment expectedseqnum */
                B_status[B_windowfirst] = BS_NONE;
                B_windowfirst = (B_windowfirst + 1) % WINDOWSIZE;
                expectedseqnum = (expectedseqnum + 1) % SEQSPACE; /* CRITICAL: Update expected base */
            }
        }

        tolayer3(B, sendpkt); /* Send the ACK */

    } else {
        /* Packet is corrupted. Discard silently. */
        if (TRACE > 0) printf("----B: packet corrupted or not expected sequence number, resend ACK!\n");
        if (expectedseqnum == 0) sendpkt.acknum = SEQSPACE - 1;
        else
            sendpkt.acknum = expectedseqnum - 1;
    }

    for (i = 0; i < 20; i++) sendpkt.payload[i] = '0'; /* No data payload in ACK */
    sendpkt.seqnum = B_nextseqnum;
    sendpkt.checksum = ComputeChecksum(sendpkt);
    B_nextseqnum = (B_nextseqnum + 1) % SEQSPACE;
    tolayer3(B, sendpkt);
}

/* the following routine will be called once (only) before any other */
/* entity B routines are called. You can use it to do any initialization */
void B_init(void) {
    int i;
    expectedseqnum = 0;
    B_nextseqnum = 1;
    B_windowfirst = 0;
    for (i = 0; i < WINDOWSIZE; i++) { B_status[i] = BS_NONE; }
}

/******************************************************************************
 * The following functions need be completed only for bi-directional messages *
 *****************************************************************************/

/* Note that with simplex transfer from a-to-B, there is no B_output() */
void B_output(struct msg message) {}

/* called when B's timer goes off */
void B_timerinterrupt(void) {}