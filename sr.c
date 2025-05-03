#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "emulator.h"
#include "sr.h"

/* ******************************************************************
   Selective Repeat protocol.  Adapted from J.F.Kurose
   and the provided Go-Back-N implementation.

   Network properties:
   - one way network delay averages five time units (longer if there
   are other messages in the channel for GBN), but can be larger
   - packets can be corrupted (either the header or the data portion)
   or lost, according to user-defined probabilities
   - packets will be delivered in the order in which they were sent
   (although some can be lost).
**********************************************************************/

/* Strictly C90 compatible boolean */
#define TRUE 1
#define FALSE 0

#define RTT  16.0       /* round trip time.  MUST BE SET TO 16.0 when submitting assignment */
#define WINDOWSIZE 6    /* the maximum number of buffered unacked packet
                          MUST BE SET TO 6 when submitting assignment */
#define SEQSPACE 14      /* the min sequence space for SR must be at least windowsize * 2. Using 2*WINDOWSIZE + 2 or more is safer.*/
#define NOTINUSE (-1)   /* used to fill header fields that are not being used*/
/* A and B are defined in emulator.h */
/* #define A 0 // Sender side */
/* #define B 1 // Receiver side */

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

int IsCorrupted(struct pkt packet)
{
  if (packet.checksum == ComputeChecksum(packet))
    return (FALSE);
  else
    return (TRUE);
}


/********* Sender (A) variables and functions ************/

static struct pkt A_packet_buffer[SEQSPACE];  /* array for storing packets waiting for ACK */
static int A_packet_sent[SEQSPACE]; /* To track if a packet has been sent (TRUE or FALSE) */
static int A_packet_acked[SEQSPACE]; /* To track if a packet has been acknowledged (TRUE or FALSE) */

static struct msg A_message_buffer[1000]; /* Buffer for messages from Layer 5 */
static int A_message_buffer_start; /* Start index of the message buffer */
static int A_message_buffer_end; /* End index of the message buffer */
static int A_message_buffer_count; /* Number of messages in the buffer */


static int A_send_base; /* Base of the sender window (sequence number) */
static int A_nextseqnum; /* The next sequence number to be used by the sender (next sequence number to assign) */

/* Helper function to send the next available packet from the message buffer */
void A_send_next_packet() {
    struct pkt sendpkt;
    int i;
    int current_seq;

    /* Check if there are messages in the buffer and space in the sender window */
    /* The window check should be based on A_nextseqnum vs A_send_base, representing
       the sequence numbers that can be sent. */
    while (A_message_buffer_count > 0 && (A_nextseqnum - A_send_base + SEQSPACE) % SEQSPACE < WINDOWSIZE) {

        current_seq = A_nextseqnum % SEQSPACE;

        if (TRACE > 3) /* Custom trace, only for higher levels */
        {
          printf("----A: Sending packet for buffered message with seq num %d\n", current_seq);
        }

        /* Create packet from the oldest buffered message */
        sendpkt.seqnum = current_seq; /* Use sequence number modulo SEQSPACE */
        sendpkt.acknum = NOTINUSE; /* ACK field not used by sender data packets */
        for ( i=0; i<20 ; i++ )
          sendpkt.payload[i] = A_message_buffer[A_message_buffer_start].data[i];
        sendpkt.checksum = ComputeChecksum(sendpkt);

        /* Put packet in packet buffer */
        A_packet_buffer[current_seq] = sendpkt; /* Store by sequence number modulo SEQSPACE */
        A_packet_sent[current_seq] = TRUE;
        A_packet_acked[current_seq] = FALSE; /* Mark as not yet acknowledged */

        /* Send out packet */
        if (TRACE > 0) /* Original GBN trace */
        {
          printf("Sending packet %d to layer 3\n", sendpkt.seqnum);
        }
        tolayer3 (A, sendpkt);

        /* Start timer if this is the first unacked packet in the window */
        /* With this simulator, we manage a single timer for entity A.
           We start the timer when the first packet in the current window is sent.
           We stop and restart the timer when an ACK for the base packet arrives
           and the window slides, IF there are still unacked packets.
           We restart the timer on timeout.
           This approximates per-packet timers for the purpose of this simulator.
        */
        /* The timer should be started if this is the packet at A_send_base */
        /* When the window is empty, A_send_base == A_nextseqnum.
           So if the next packet to be sent is the new window base, start the timer. */
        if ( (A_nextseqnum % SEQSPACE) == (A_send_base % SEQSPACE) )
        {
             starttimer(A, RTT);
             if (TRACE > 3) /* Custom trace */
             {
               printf("----A: Starting timer for window base %d\n", A_send_base % SEQSPACE);
             }
        }


        /* Move to the next message in the buffer */
        A_message_buffer_start = (A_message_buffer_start + 1) % 1000;
        A_message_buffer_count--;

        /* Increment next sequence number for assignment */
        A_nextseqnum++;
    }
}


/* called from layer 5 (application layer), passed the message to be sent to other side*/
void A_output(struct msg message)
{
    int current_packet_window_count;

    /* Calculate current number of packets in the sender's packet window */
    current_packet_window_count = (A_nextseqnum - A_send_base + SEQSPACE) % SEQSPACE;


    /* Always print a message arrival trace if TRACE > 1, similar to GBN */
    /* In GBN, this message is tied to whether the window is full or not.
       Since we buffer Layer 5 messages, the message is always accepted by the transport layer
       (unless the message buffer is full). Let's adapt the GBN message printing
       based on the state of the *packet* window when A_output is called.
    */
    if (TRACE > 1)
    {
        if (current_packet_window_count < WINDOWSIZE)
        {
             printf("----A: New message arrives, send window is not full, send new messge to layer3!\n");
        } else
        {
             /* Packet window is full when A_output is called. In GBN this means dropping.
                In SR, we buffer the Layer 5 message. Let's only print the GBN "window full"
                message if the message buffer itself is full (very rare).
             */
             if (A_message_buffer_count >= 1000)
             {
                  if (TRACE > 0) /* Original GBN trace for window full */
                  {
                    printf("----A: New message arrives, send window is full\n");
                  }
             } else
             {
                 /* Packet window is full, but message buffer is not. Message is buffered.
                    No original GBN trace directly for this state. */
                 if (TRACE > 3) /* Custom trace */
                 {
                   printf("----A: New message arrives, packet window full, buffering message.\n");
                 }
             }
        }
    }


    /* Buffer the incoming message from Layer 5 */
    if (A_message_buffer_count < 1000) /* Check if message buffer is not full */
    {

        A_message_buffer[A_message_buffer_end] = message;
        A_message_buffer_end = (A_message_buffer_end + 1) % 1000;
        A_message_buffer_count++;

        if (TRACE > 3) /* Custom trace, only for higher levels */
        {
            printf("----A: Message buffered from layer 5. Buffer count: %d\n", A_message_buffer_count);
        }

        /* Try to send packets from the buffer if there is space in the packet window */
        A_send_next_packet();

    } else
    {
        /* This block is for when the message buffer is full - very rare with size 1000 for 20 messages */
        /* The GBN "window full" print is already handled above based on message buffer state. */
        window_full++; /* Use the provided statistic for dropped messages */
         if (TRACE > 3) /* Custom trace */
         {
           printf("----A: Message buffer is full, dropping message from layer 5\n");
         }
    }
}


/* called from layer 3, when a packet arrives for layer 4
   In this practical this will always be an ACK as B never sends data.
*/
void A_input(struct pkt packet)
{
  int acked_seq;
  int i;
  int any_unacked; /* Use int for boolean */
  int sent_packet_count;
  int ack_offset;
  int is_in_sent_range; /* Use int for boolean */


  /* if received ACK is not corrupted */
  if (!IsCorrupted(packet))
  {
    if (TRACE > 0) /* Original GBN trace */
    {
      printf("----A: uncorrupted ACK %d is received\n",packet.acknum);
    }
    total_ACKs_received++;

    acked_seq = packet.acknum;

    /* Check if the acknowledged packet's sequence number is within the logical range
       of sent but not yet acknowledged packets: [A_send_base, A_nextseqnum - 1].
       Use sequence number comparison that handles wrap-around.
       A sequence number 's' is within this range if (s - A_send_base + SEQSPACE) % SEQSPACE
       is less than the number of packets sent but not yet processed by A_input (which is A_nextseqnum - A_send_base).
    */
    sent_packet_count = (A_nextseqnum - A_send_base + SEQSPACE) % SEQSPACE;
    ack_offset = (acked_seq - A_send_base + SEQSPACE) % SEQSPACE;

    is_in_sent_range = FALSE;
    if (ack_offset < sent_packet_count)
    {
        is_in_sent_range = TRUE;
    }


    if (is_in_sent_range)
    {
        if (TRACE > 3) /* Custom trace */
        {
          printf("----A: ACK %d is for a packet in the sent range\n", acked_seq);
        }
        new_ACKs++;

        /* Mark the packet as acknowledged */
        A_packet_acked[acked_seq % SEQSPACE] = TRUE;
        /* Stop the timer for this specific packet if using per-packet timers.
           With this simulator, we adjust the single timer based on window movement. */

        /* Slide the window if the base packet has been acknowledged */
        /* Window slides as long as the current A_send_base packet is acknowledged
           and there are still packets in the logical sent range (A_send_base < A_nextseqnum) */
        while (A_packet_acked[A_send_base % SEQSPACE] == TRUE && (A_send_base - A_nextseqnum + SEQSPACE) % SEQSPACE != 0)
        {
            if (TRACE > 3) /* Custom trace */
            {
              printf("----A: Packet %d acknowledged, sliding window\n", A_send_base % SEQSPACE);
            }
            A_packet_sent[A_send_base % SEQSPACE] = FALSE; /* Reset sent status */
            A_packet_acked[A_send_base % SEQSPACE] = FALSE; /* Reset acked status for reuse */
            A_send_base++; /* Slide the window base */

            /* Manage the single timer: stop it and restart if there are still unacked packets
               in the new window [A_send_base, A_nextseqnum - 1]. */
            stoptimer(A); /* Stop the current timer */
            any_unacked = FALSE;
             /* Check if there are any unacked packets in the window [A_send_base, A_nextseqnum - 1] */
             /* Iterate from the new A_send_base up to A_nextseqnum */
            for(i = A_send_base; (i - A_nextseqnum + SEQSPACE) % SEQSPACE != 0; i++)
            {
                 if(A_packet_sent[i % SEQSPACE] == TRUE && A_packet_acked[i % SEQSPACE] == FALSE) /* Check if sent and NOT acknowledged */
                 {
                     any_unacked = TRUE;
                     break;
                 }
             }

            if(any_unacked)
            {
                starttimer(A, RTT); /* Start the timer if there are still unacked packets */
                 if (TRACE > 3) /* Custom trace */
                 {
                   printf("----A: Unacked packets remain, restarting timer for window base %d\n", A_send_base % SEQSPACE);
                 }
            } else
            {
                 /* If no unacked packets, timer remains stopped */
                 if (TRACE > 3) /* Custom trace */
                 {
                    printf("----A: No unacked packets in window, timer remains stopped.\n");
                 }
            }
        }

        /* After sliding the window, try to send more packets from the message buffer */
        A_send_next_packet();

    } else
    {
         /* This is an ACK for a packet *outside* the logical sent range [A_send_base, A_nextseqnum - 1].
            If the ACK sequence number is less than A_send_base, it's an old duplicate ACK.
            If it's >= A_nextseqnum (accounting for wrap), it's an ACK for a future packet (shouldn't happen or discard).
            Given the trace requirement, let's print the GBN duplicate ACK message for any uncorrupted ACK outside the sent range.
         */
         if (TRACE > 0) /* Original GBN trace for duplicate ACK */
         {
           printf ("----A: duplicate ACK received, do nothing!\n");
         }
         if (TRACE > 3) /* Custom trace */
         {
           printf ("----A: Received ACK %d for packet outside the current sent range\n", acked_seq);
         }

    }
  } else
  {
    if (TRACE > 0) /* Original GBN trace */
    {
      printf ("----A: corrupted ACK is received, do nothing!\n");
    }
  }
}

/* called when A's timer goes off*/
void A_timerinterrupt(void)
{
  int i;
  int current_seq;

  if (TRACE > 0) /* Original GBN trace */
  {
    printf("----A: time out,resend packets!\n");
  }

  /* Restart the overall timer immediately */
  stoptimer(A); /* Stop the expired timer before restarting */
  starttimer(A, RTT);
  if (TRACE > 3) /* Custom trace */
  {
    printf("----A: Restarting timer for window base %d after timeout\n", A_send_base % SEQSPACE);
  }


  /* Check for unacknowledged packets within the window [A_send_base, A_nextseqnum - 1] and resend them */
  /* Iterate from A_send_base up to A_nextseqnum */
  for (i = A_send_base; (i - A_nextseqnum + SEQSPACE) % SEQSPACE != 0; i++)
  {
      current_seq = i % SEQSPACE;
      /* Use A_packet_sent to ensure we only resend packets that were actually sent */
      if (A_packet_sent[current_seq] == TRUE && A_packet_acked[current_seq] == FALSE) /* Check if sent and NOT acknowledged */
      {
          if (TRACE > 0) /* Original GBN trace */
          {
              printf ("---A: resending packet %d\n", A_packet_buffer[current_seq].seqnum);
          }
          tolayer3(A, A_packet_buffer[current_seq]);
          packets_resent++;
          /* In a real SR implementation, you would restart the timer for this specific packet here.
             With this simulator, the single timer covers the window, so we just resend and the timer is already restarted above. */
      }
  }
}


/* the following routine will be called once (only) before any other */
/* entity A routines are called. You can use it to do any initialization*/
void A_init(void)
{
  int i;
  A_send_base = 0;
  A_nextseqnum = 0; /* A_nextseqnum is the sequence number to be used for the next *new* packet */
  for(i = 0; i < SEQSPACE; i++)
  {
      A_packet_sent[i] = FALSE;
      A_packet_acked[i] = FALSE;
      /* Optional: Initialize packet buffer entries */
      /* memset(&A_packet_buffer[i], 0, sizeof(struct pkt)); */
  }
  A_message_buffer_start = 0;
  A_message_buffer_end = 0;
  A_message_buffer_count = 0;
}



/********* Receiver (B)  variables and procedures ************/

static int B_expectedseqnum; /* The sequence number expected next by the receiver for in-order delivery */
static struct pkt B_packet_buffer[SEQSPACE]; /* Buffer for out-of-order packets */
static int B_packet_buffered[SEQSPACE]; /* To track if a packet is buffered in the receiver buffer (TRUE or FALSE) */
static int B_packet_received_status[SEQSPACE]; /* To track status: 0=Not Received, 1=Received but not delivered, 2=Received and Delivered */


/* called from layer 3, when a packet arrives for layer 4 at B*/
void B_input(struct pkt packet)
{
  int i;
  int received_seq;
  int packet_offset_from_base;
  struct pkt sendpkt; /* Declare sendpkt at the beginning */


  /* create ACK packet to send immediately */
  sendpkt.seqnum = 0; /* Sender doesn't care about this for ACKs */
  sendpkt.payload[0] = '0'; /* No data in ACK packet */
  for(i=1; i<20; i++)
  {
    sendpkt.payload[i] = '0'; /* Fill the rest with 0s */
  }


  if (!IsCorrupted(packet))
  {
    if (TRACE > 3) /* Custom trace */
    {
      printf("----B: uncorrupted packet %d received\n", packet.seqnum);
    }
    packets_received++;

    received_seq = packet.seqnum;
    /* The receive window is [B_expectedseqnum, B_expectedseqnum + WINDOWSIZE - 1] */
    /* A packet with sequence number 's' is within the window if (s - B_expectedseqnum + SEQSPACE) % SEQSPACE < WINDOWSIZE */
    packet_offset_from_base = (received_seq - B_expectedseqnum + SEQSPACE) % SEQSPACE;


    /* --- Start of original GBN trace logic mapping --- */
    /* In GBN, the print logic depends on whether the packet is corrupted OR has the expected sequence number. */
    if (received_seq == B_expectedseqnum)
    {
         if (TRACE > 0) /* Original GBN print for correct in-order */
         {
            printf("----B: packet %d is correctly received, send ACK!\n", packet.seqnum);
         }
    } else
    {
        /* If uncorrupted BUT NOT the expected sequence number.
           In GBN this means corrupted OR not expected sequence number.
           In SR, this covers out-of-order within the window or old duplicates. */
        if (TRACE > 0) /* Original GBN print for corrupted OR not expected */
        {
             printf("----B: packet corrupted or not expected sequence number, resend ACK!\n");
        }
    }
    /* --- End of original GBN trace logic mapping --- */


    /* Check if the received packet is within the receiver's window [B_expectedseqnum, B_expectedseqnum + WINDOWSIZE - 1] */
    if (packet_offset_from_base < WINDOWSIZE)
    {
        if (TRACE > 3) /* Custom trace */
        {
          printf("----B: packet %d is within the window\n", received_seq);
        }

        /* Send an ACK for the received packet, even if out of order */
        sendpkt.acknum = received_seq;
        sendpkt.checksum = ComputeChecksum(sendpkt);
        tolayer3(B, sendpkt);
        if (TRACE > 3) /* Custom trace */
        {
          printf("----B: sending ACK for packet %d\n", received_seq);
        }

        /* If the packet is the expected one, deliver it and any buffered in-order packets */
        if (received_seq == B_expectedseqnum)
        {
            if (TRACE > 3) /* Custom trace */
            {
              printf("----B: packet %d is the expected one, delivering\n", received_seq);
            }
            tolayer5(B, packet.payload);
            B_packet_received_status[B_expectedseqnum % SEQSPACE] = 2; /* Mark as received and delivered */
            B_expectedseqnum++;

            /* Deliver any buffered packets that are now in order */
            while(B_packet_buffered[B_expectedseqnum % SEQSPACE] == TRUE && B_packet_received_status[B_expectedseqnum % SEQSPACE] == 1) /* Check if buffered and received but not delivered */
            {
                 if (TRACE > 3) /* Custom trace */
                 {
                   printf("----B: delivering buffered packet %d\n", B_expectedseqnum % SEQSPACE);
                 }
                tolayer5(B, B_packet_buffer[B_expectedseqnum % SEQSPACE].payload);
                B_packet_buffered[B_expectedseqnum % SEQSPACE] = FALSE; /* Mark as not buffered */
                B_packet_received_status[B_expectedseqnum % SEQSPACE] = 2; /* Mark as received and delivered */
                /* Reset packet buffer entry after delivery if needed (optional but good practice) */
                /* memset(&B_packet_buffer[B_expectedseqnum % SEQSPACE], 0, sizeof(struct pkt)); */
                B_expectedseqnum++;
            }

        } else
        { /* If the packet is out of order but within the window and not already received/buffered, buffer it */
            /* Check if this packet has already been received (either buffered or delivered) */
            if (B_packet_received_status[received_seq % SEQSPACE] == 0) /* If not received before */
            {
                 if (TRACE > 3) /* Custom trace */
                 {
                   printf("----B: packet %d is out of order, buffering\n", received_seq);
                 }
                B_packet_buffer[received_seq % SEQSPACE] = packet; /* Buffer the packet */
                B_packet_buffered[received_seq % SEQSPACE] = TRUE; /* Mark as buffered */
                B_packet_received_status[received_seq % SEQSPACE] = 1; /* Mark as received but not delivered */
            } else
            {
                 if (TRACE > 3) /* Custom trace */
                 {
                   printf("----B: packet %d already received or buffered\n", received_seq);
                 }
            }
        }

    } else
    { /* Packet is outside the window (too old or too far ahead) */
        /* If the packet is too old but not corrupted (< B_expectedseqnum), it's an old duplicate.
           Resend ACK for such duplicates. */
         if (received_seq < B_expectedseqnum)
         {
             if (TRACE > 3) /* Custom trace */
             {
               printf("----B: old duplicate packet %d received, resending ACK\n", received_seq);
             }
             sendpkt.acknum = received_seq;
             sendpkt.checksum = ComputeChecksum(sendpkt);
             tolayer3(B, sendpkt);
             if (TRACE > 3) /* Custom trace */
             {
               printf("----B: sending ACK for packet %d\n", received_seq);
             }
             /* The GBN "not expected sequence number" print for old duplicates is handled above based on != expected_seq. */

         } else
         {
             /* Packet is too far ahead (> B_expectedseqnum + WINDOWSIZE - 1). Discard. */
             if (TRACE > 3) /* Custom trace */
             {
               printf("----B: packet %d is too far ahead or outside the window, discarding\n", received_seq);
             }
             /* No original GBN print for this specific case, it falls under "not expected". */
         }


    }
  } else
  { /* Corrupted packet */
    if (TRACE > 0) /* Original GBN trace */
    {
      printf ("----B: corrupted packet received, do nothing!\n"); /* GBN uses this for corrupted */
    }
    if (TRACE > 3) /* Custom trace */
    {
      printf ("----B: corrupted packet received, discarding\n");
    }
  }
}

/* the following routine will be called once (only) before any other */
/* entity B routines are called. You can use it to do any initialization*/
void B_init(void)
{
  int i;
  B_expectedseqnum = 0;
  for(i = 0; i < SEQSPACE; i++)
  {
      B_packet_buffered[i] = FALSE;
      B_packet_received_status[i] = 0; /* 0=Not Received */
      /* Optional: Initialize packet buffer entries */
      /* memset(&B_packet_buffer[i], 0, sizeof(struct pkt)); */
  }
}

/******************************************************************************
 * The following functions need be completed only for bi-directional messages *
 *****************************************************************************/

/* Note that with simplex transfer from a-to-B, there is no B_output()*/
void B_output(struct msg message)
{
}

/* called when B's timer goes off*/
void B_timerinterrupt(void)
{
}