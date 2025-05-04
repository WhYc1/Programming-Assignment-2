#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h> /* Added for memcpy */
#include "emulator.h"
#include "sr.h" /* Changed from gbn.h */

/* ******************************************************************
   Selective Repeat protocol.  Adapted from J.F.Kurose
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
   - added GBN implementation (Base for SR)
   - Implemented complete Selective Repeat protocol (C90 Compliant)
   - Addressed issues with sender timer/retransmission and receiver
     window/buffering/delivery logic based on trace analysis.
   - Corrected remaining C90 compliance warnings.
**********************************************************************/

#define RTT  16.0       /* round trip time.  MUST BE SET TO 16.0 when submitting assignment */
#define WINDOWSIZE 6    /* the maximum number of buffered unacked packet
                          MUST BE SET TO 6 when submitting assignment */
#define SEQSPACE 7      /* the min sequence space for GBN must be at least windowsize + 1 */
#define NOTINUSE (-1)   /* used to fill header fields that are not being used */

/* generic procedure to compute the checksum of a packet.  Used by both sender and receiver
   the simulator will overwrite part of your packet with 'z's.  It will not overwrite your
   original checksum.  This procedure must generate a different checksum to the original if
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

static struct pkt buffer[WINDOWSIZE];  /* array for storing packets waiting for ACK */
static bool packet_acked[WINDOWSIZE];  /* flag for each packet in buffer indicating if ACK has been received */
static int windowfirst;                /* Index in buffer of the first packet in the window (always 0) */
static int windowcount;                /* the number of packets currently awaiting an ACK */
static int A_nextseqnum;               /* the next sequence number to be used by the sender */
static int base;                       /* base of the sender window (sequence number) */
static bool timer_running;             /* Flag to track if the timer is currently running */


/* called from layer 5 (application layer), passed the message to be sent to other side */
void A_output(struct msg message)
{
  struct pkt sendpkt;
  int i; /* Variable declaration moved to the beginning */
  bool has_unacked; /* Variable declaration moved to the beginning */


  /* SR sender window is [base, base + WINDOWSIZE - 1] (modulo SEQSPACE) */
  /* The window in the buffer is from index 0 to windowcount - 1 */
  if (windowcount < WINDOWSIZE) { /* If window is not full */
    if (TRACE > 1)
      printf("----A: New message arrives, send window is not full, send new messge to layer3!\n");

    /* create packet */
    sendpkt.seqnum = A_nextseqnum;
    sendpkt.acknum = NOTINUSE; /* Sender doesn't use ACK num in data packets */
    for ( i=0; i<20 ; i++ )
      sendpkt.payload[i] = message.data[i];
    sendpkt.checksum = ComputeChecksum(sendpkt);

    /* put packet in window buffer */
    /* The new packet is always placed at the end of the active window in the buffer */
    /* The buffer array elements 0 to windowcount-1 correspond to the current window. */
    buffer[windowcount] = sendpkt; /* Store the packet */
    packet_acked[windowcount] = false; /* Mark as not yet acked */
    windowcount++; /* Increase window count */

    /* send out packet */
    if (TRACE > 0)
      printf("Sending packet %d (buffer idx %d) to layer 3\n", sendpkt.seqnum, windowcount - 1);
    tolayer3 (A, sendpkt);

    /* Start timer if there are any unacked packets in the window and timer is not running */
    /* Check if there's at least one unacked packet in the window */
    has_unacked = false;
    for(i = 0; i < windowcount; i++) {
        if (!packet_acked[i]) {
            has_unacked = true;
            break;
        }
    }

    if (has_unacked && !timer_running) {
         starttimer(A,RTT);
         timer_running = true;
         if (TRACE > 0)
             printf("----A: Starting timer as there are unacked packets and timer is not running.\n");
    }


    /* get next sequence number, wrap back to 0 */
    A_nextseqnum = (A_nextseqnum + 1) % SEQSPACE;
  }
  /* if blocked,  window is full */
  else {
    if (TRACE > 0)
      printf("----A: New message arrives, send window is full, dropping message\n");
    window_full++;
  }
}


/* called from layer 3, when a packet arrives for layer 4
   In this practical this will always be an ACK as B never sends data.
*/
void A_input(struct pkt packet)
{
  int i; /* Variable declaration moved to the beginning */
  int acked_seq = packet.acknum;
  bool in_window; /* Variable declaration moved to the beginning */
  int seq_diff_from_base; /* Variable declaration moved to the beginning */
  int buffer_index_in_window; /* Variable declaration moved to the beginning */
  bool has_unacked_after_sliding; /* Variable declaration moved to the beginning */
  int old_windowcount; /* Variable declaration moved to the beginning */


  /* if received ACK is not corrupted */
  if (!IsCorrupted(packet)) {
    if (TRACE > 0)
      printf("----A: uncorrupted ACK %d is received\n", acked_seq);
    total_ACKs_received++;

    /* Check if the ACKed sequence number is within the sender's window [base, base + windowcount - 1] (modulo SEQSPACE) */
    in_window = false;
    /* Check if a sequence number is within the sender's current active window */
    seq_diff_from_base = (acked_seq - base + SEQSPACE) % SEQSPACE;

    if (seq_diff_from_base < windowcount) {
        in_window = true;
    }


    if (in_window) {
        if (TRACE > 0)
            printf("----A: ACK %d is within the sender window (base %d, count %d)\n", acked_seq, base, windowcount);
        /* Only count as new ACK if it was previously unacked */
        /* Calculate the buffer index relative to the window base */
        buffer_index_in_window = seq_diff_from_base; /* This is simply the difference in sequence numbers, which is the buffer index */

        /* Check if the calculated index is actually within the valid buffer range (0 to windowcount-1) */
        if (buffer_index_in_window < windowcount) {
             if (!packet_acked[buffer_index_in_window]) {
                 new_ACKs++;
                 packet_acked[buffer_index_in_window] = true;
                 if (TRACE > 0)
                    printf("----A: packet %d (buffer index %d) marked as acked\n", acked_seq, buffer_index_in_window);
             } else {
                 if (TRACE > 0)
                    printf("----A: duplicate ACK for packet %d received\n", acked_seq);
             }

            /* Slide the window if packets at the base are acked */
            /* While window is not empty and the first packet in buffer is acked */
            old_windowcount = windowcount; /* Store original window count before sliding */
            while (windowcount > 0 && packet_acked[0]) {
                if (TRACE > 0)
                    printf("----A: window base packet %d (buffer index 0) is acked, sliding window\n", buffer[0].seqnum);

                /* Slide the window base (sequence number) */
                base = (base + 1) % SEQSPACE;
                windowcount--;

                /* Shift buffer content and acked flags to the left */
                /* Shift old_windowcount - 1 elements */
                for (i = 0; i < old_windowcount - 1; i++) {
                    buffer[i] = buffer[i+1];
                    packet_acked[i] = packet_acked[i+1];
                }
                 /* The last position in the logical window (index windowcount after sliding) */
                 /* will be filled by a new packet. We don't need to explicitly clear buffer[windowcount] */
                 /* or packet_acked[windowcount] here, as windowcount has decreased. */
                 /* The last physical position in the buffer (WINDOWSIZE-1) can be cleared */
                 /* as it's now outside the range of valid packets. */
                 packet_acked[WINDOWSIZE - 1] = false; /* Clear flag for the shifted out position */


                 if (TRACE > 0)
                    printf("----A: sender window slides to base %d, new windowcount %d\n", base, windowcount);
                 old_windowcount--; /* Decrement old_windowcount as we slide */
            }

            /* After sliding, manage the timer */
            /* If window is not empty, check if there are unacked packets and restart timer if needed. If empty, stop timer. */
            if (windowcount > 0) {
                 /* Check if there are still unacked packets in the remaining window */
                 has_unacked_after_sliding = false;
                 for(i = 0; i < windowcount; i++) {
                     if (!packet_acked[i]) {
                         has_unacked_after_sliding = true;
                         break;
                     }
                 }

                 if (has_unacked_after_sliding && !timer_running) {
                     starttimer(A, RTT); /* Start timer for the new state */
                     timer_running = true;
                      if (TRACE > 0)
                        printf("----A: Window not empty and has unacked, starting timer.\n");
                 } else if (!has_unacked_after_sliding && timer_running) {
                      stoptimer(A); /* Stop timer as all packets in window are now acked */
                      timer_running = false;
                       if (TRACE > 0)
                         printf("----A: Window not empty but all packets acked, stopping timer.\n");
                 }
                 /* If has_unacked_after_sliding && timer_running, timer is already running for unacked packets, do nothing */


            } else { /* Window is empty after sliding */
                if (timer_running) {
                    stoptimer(A); /* Stop timer as window is empty */
                    timer_running = false;
                     if (TRACE > 0)
                        printf("----A: Window is empty after sliding, stopping timer.\n");
                }
            }


        } else {
             if (TRACE > 0)
                printf("----A: Calculated buffer index %d for ACK %d is out of current window range (0 to %d), ignoring.\n", buffer_index_in_window, acked_seq, windowcount > 0 ? windowcount - 1 : 0);
        }


    } else {
        if (TRACE > 0)
          printf("----A: ACK %d is outside the sender window (base %d, count %d), ignoring\n", acked_seq, base, windowcount);
    }
  }
  else {
    if (TRACE > 0)
      printf ("----A: corrupted ACK is received, ignoring\n");
  }
}

/* called when A's timer goes off */
void A_timerinterrupt(void)
{
  int i; /* Variable declaration moved to the beginning */
  bool has_unacked_after_timeout; /* Variable declaration moved to the beginning */

  if (TRACE > 0)
    printf("----A: timer expired, resending packets!\n");
  packets_resent++; /* Increment resend count (cumulative for this timeout event) - Following GBN's statistic usage */
  timer_running = false; /* Timer has just expired */

  /* Resend all packets currently in the window (from buffer index 0 to windowcount - 1) */
  /* This simulates retransmitting all unacked packets upon timeout of the oldest unacked packet's timer. */
  has_unacked_after_timeout = false; /* Check if there are still unacked packets after resending */
  for(i = 0; i < windowcount; i++) {
      /* Check if this packet is unacked */
      if (!packet_acked[i]) {
          if (TRACE > 0)
              printf ("---A: resending unacked packet %d (buffer index %d)\n", buffer[i].seqnum, i);
          tolayer3(A, buffer[i]);
          /* packets_resent++; Removed here to avoid double counting if incremented above */
          has_unacked_after_timeout = true;
      }
  }

  /* Restart timer if there are still unacked packets in the window after resending */
  if (has_unacked_after_timeout) {
      starttimer(A, RTT);
      timer_running = true;
       if (TRACE > 0)
           printf("----A: Still unacked packets after timeout and resend, restarting timer.\n");
  } else {
       if (TRACE > 0)
           printf("----A: No unacked packets after timeout and resend, timer remains stopped.\n");
  }
}


/* the following routine will be called once (only) before any other */
/* entity A routines are called. You can use it to do any initialization */
void A_init(void)
{
  int i; /* Variable declaration moved to the beginning */

  /* initialise A's window, buffer, sequence number, base and timer flag */
  A_nextseqnum = 0;  /* A starts with seq num 0, do not change this */
  base = 0;          /* Sender window base starts at 0 (sequence number) */
  windowfirst = 0;   /* Index in buffer of the first packet in the window (always 0 in this buffer structure) */
  /* windowlast is not strictly necessary with this buffer structure, windowcount implies the last position. */
  windowcount = 0;   /* Number of packets currently in the window awaiting ACK */
  timer_running = false; /* Timer is initially not running */


  /* Initialize the packet_acked flags and buffer content (optional, good practice) */
  for (i = 0; i < WINDOWSIZE; i++) { /* Correct C90 for loop */
      packet_acked[i] = false;
      /* Optionally clear buffer content */
      buffer[i].seqnum = NOTINUSE;
      buffer[i].acknum = NOTINUSE;
      buffer[i].checksum = 0;
      memset(buffer[i].payload, 0, 20);
  }
  if (TRACE > 0)
      printf("----A: Initialized sender buffer, acked flags, base %d, and timer flag.\n", base);
}

/********* Receiver (B)  variables and procedures ************/

static int expectedseqnum; /* the sequence number expected next by the receiver */
static int B_nextseqnum;   /* the sequence number for the next packets sent by B (used for ACK seqnum) */

/* SR Receiver specific variables */
static struct pkt rcv_buffer[WINDOWSIZE]; /* Buffer for storing received packets */
static bool buffered[WINDOWSIZE];         /* To track which positions in the rcv_buffer are occupied relative to expectedseqnum */


/* called from layer 3, when a packet arrives for layer 4 at B*/
void B_input(struct pkt packet)
{
  struct pkt sendpkt;
  int i; /* Variable declaration moved to the beginning */
  int packet_seqnum = packet.seqnum;
  bool in_window; /* Variable declaration moved to the beginning */
  int seq_diff_from_expected; /* Variable declaration moved to the beginning */
  int buffer_index; /* Variable declaration moved to the beginning */
  int diff_to_expected; /* Variable declaration moved to the beginning */


  /* if not corrupted */
  if (!IsCorrupted(packet)) {
    if (TRACE > 0)
      printf("----B: uncorrupted packet %d received\n", packet_seqnum);
    packets_received++;

    /* Check if the received packet is within the receiver's window [expectedseqnum, expectedseqnum + WINDOWSIZE - 1] (modulo SEQSPACE) */
    in_window = false;
    /* The valid sequence numbers for the receiver's window relative to expectedseqnum are 0 to WINDOWSIZE-1 (modulo SEQSPACE) */
    seq_diff_from_expected = (packet_seqnum - expectedseqnum + SEQSPACE) % SEQSPACE;

    if (seq_diff_from_expected < WINDOWSIZE) {
         in_window = true;
    }

    if (in_window) {
        if (TRACE > 0)
            printf("----B: packet %d is within the receiver window (expected %d)\n", packet_seqnum, expectedseqnum);

        /* Send individual ACK for the received packet */
        sendpkt.seqnum = B_nextseqnum; /* Can be a simple counter */
        sendpkt.acknum = packet_seqnum; /* ACK the specific sequence number */
        /* we don't have any data to send back.  fill payload with 0's */
        for ( i=0; i<20 ; i++ )
            sendpkt.payload[i] = '0';
        sendpkt.checksum = ComputeChecksum(sendpkt);
        tolayer3(B, sendpkt);
        if (TRACE > 0)
            printf("----B: sending ACK for packet %d\n", packet_seqnum);


        /* Calculate buffer index relative to expectedseqnum */
        buffer_index = seq_diff_from_expected; /* This is simply the difference */

        /* Buffer the packet if it's a new packet within the window */
        if (!buffered[buffer_index]) {
             /* Correctly copy the packet content */
             rcv_buffer[buffer_index].seqnum = packet.seqnum;
             rcv_buffer[buffer_index].acknum = packet.acknum; /* ACK num in data packet is NOTINUSE for uni-directional */
             rcv_buffer[buffer_index].checksum = packet.checksum;
             memcpy(rcv_buffer[buffer_index].payload, packet.payload, 20);

            buffered[buffer_index] = true;
            if (TRACE > 0)
                 printf("----B: buffering packet %d at relative index %d (expected seq %d)\n", packet_seqnum, buffer_index, expectedseqnum);
        } else {
             if (TRACE > 0)
                 printf("----B: packet %d is a duplicate within the window, already buffered\n", packet_seqnum);
        }


        /* Check if packets can be delivered to Layer 5 */
        while (buffered[0]) { /* While the packet at the start of the window (relative index 0) is buffered */
            if (TRACE > 0)
                printf("----B: delivering packet %d to layer 5\n", expectedseqnum);
            tolayer5(B, rcv_buffer[0].payload);

            /* Slide the receiver window base */
            expectedseqnum = (expectedseqnum + 1) % SEQSPACE;

            /* Shift buffered packets and flags to the left */
            for (i = 0; i < WINDOWSIZE - 1; i++) {
                rcv_buffer[i] = rcv_buffer[i+1];
                buffered[i] = buffered[i+1];
            }
            /* Mark the last position as not buffered (it's now the new end of the window span) */
            buffered[WINDOWSIZE - 1] = false;


             if (TRACE > 0)
                 printf("----B: receiver window slides to expected seq %d\n", expectedseqnum);
        }

    } else {
      if (TRACE > 0)
        printf("----B: packet %d is outside the receiver window (expected %d), discarding or resending ACK for duplicate\n", packet_seqnum, expectedseqnum);
       /* If packet is outside the window: */
       /* - If seqnum < expectedseqnum: It's a duplicate of an already delivered packet. Resend ACK. */
       /* - If seqnum >= expectedseqnum + WINDOWSIZE: It's too far ahead, discard. */

        /* Check if it's a past packet (outside window and not a future packet) */
        /* A packet is a past packet if its sequence number is less than expectedseqnum,
           considering wrap around. This happens when (packet_seqnum - expectedseqnum + SEQSPACE) % SEQSPACE
           is a large value close to SEQSPACE.
           Conversely, a packet is too far ahead if its sequence number is >= expectedseqnum + WINDOWSIZE,
           considering wrap around. This happens when (packet_seqnum - expectedseqnum + SEQSPACE) % SEQSPACE
           is >= WINDOWSIZE.
           Since we already checked seq_diff_from_expected < WINDOWSIZE for in_window,
           if it's not in_window, then seq_diff_from_expected >= WINDOWSIZE.
           We need to differentiate between "too far ahead" (seq_diff_from_expected >= WINDOWSIZE)
           and "past packet" (seq_diff_from_expected corresponds to a value before expectedseqnum).

           Let's use the difference to expectedseqnum to check if it's a past packet.
        */
        diff_to_expected = (packet_seqnum - expectedseqnum + SEQSPACE) % SEQSPACE; /* Calculate difference to expectedseqnum */


        if (diff_to_expected >= WINDOWSIZE) {
             if (TRACE > 0)
                printf("----B: packet %d is too far ahead, discarding\n", packet_seqnum);
         } else { /* If not too far ahead, and not in_window, it must be a past packet */
               if (TRACE > 0)
                  printf("----B: packet %d is a duplicate (already delivered), resending ACK\n", packet_seqnum);
               sendpkt.seqnum = B_nextseqnum;
               sendpkt.acknum = packet_seqnum;
               for ( i=0; i<20 ; i++ )
                  sendpkt.payload[i] = '0';
               sendpkt.checksum = ComputeChecksum(sendpkt);
               tolayer3(B, sendpkt);
                if (TRACE > 0)
                    printf("----B: resending ACK for packet %d\n", packet_seqnum);
         }
    }
  }
  else {
    if (TRACE > 0)
      printf ("----B: corrupted packet received, discarding\n");
  }

  B_nextseqnum = (B_nextseqnum + 1) % 2;
}

/* the following routine will be called once (only) before any other */
/* entity B routines are called. You can use it to do any initialization */
void B_init(void)
{
  int i; /* Variable declaration moved to the beginning */

  expectedseqnum = 0;
  B_nextseqnum = 1; /* Initial sequence number for ACKs from B (can be anything, just needs to change for checksum) */

  /* Initialize SR receiver buffer and flags */
  for (i = 0; i < WINDOWSIZE; i++) { /* Correct C90 for loop */
      buffered[i] = false;
      /* Optionally clear buffer content */
       rcv_buffer[i].seqnum = NOTINUSE;
       rcv_buffer[i].acknum = NOTINUSE;
       rcv_buffer[i].checksum = 0;
       memset(rcv_buffer[i].payload, 0, 20);
  }
   if (TRACE > 0)
       printf("----B: Initialized receiver buffer and flags, expected seq %d.\n", expectedseqnum);
}

/******************************************************************************
 * The following functions need be completed only for bi-directional messages *
 *****************************************************************************/

/* Note that with simplex transfer from a-to-B, there is no B_output() */
void B_output(struct msg message)
{
}

/* called when B's timer goes off */
void B_timerinterrupt(void)
{
}