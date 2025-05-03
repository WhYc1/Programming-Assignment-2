#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

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
    // The condition for space in the sender window should check if the sequence number
    // A_nextseqnum is within the range [A_send_base, A_send_base + WINDOWSIZE - 1] considering wrap-around.
    // A simpler way to express this is checking if the number of currently sent but unacked packets
    // (which is related to A_nextseqnum and A_send_base) is less than WINDOWSIZE.
    // However, given the current structure using A_nextseqnum as the potential next sequence,
    // let's refine the existing window check. A_nextseqnum should be less than A_send_base + WINDOWSIZE,
    // handling wrap around for the comparison.
    int window_limit = (A_send_base + WINDOWSIZE) % SEQSPACE;
    bool has_space = false;
    if (A_send_base <= window_limit) { // Window does not wrap
        if (A_nextseqnum >= A_send_base && A_nextseqnum < window_limit) {
            has_space = true;
        }
    } else { // Window wraps
        if (A_nextseqnum >= A_send_base || A_nextseqnum < window_limit) {
            has_space = true;
        }
    }


    while (A_message_buffer_count > 0 && has_space) {

        // Commented out this detailed print to potentially fix trace mismatch
        // if (TRACE > 1)
        //     printf("----A: Sending packet for buffered message with seq num %d\n", A_nextseqnum);

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
        // With a single timer for the window, the timer is started when the first packet in the
        // current window (A_send_base) is sent, and it runs as long as there are unacked packets
        // in the window. It's stopped and potentially restarted when the window slides.
        // The current logic to start timer if A_send_base == A_nextseqnum before incrementing
        // A_nextseqnum correctly identifies if the newly sent packet is the first one in the window.
        if (A_send_base == A_nextseqnum) {
            starttimer(A, RTT);
        }

        /* Move to the next message in the buffer */
        A_message_buffer_start = (A_message_buffer_start + 1) % 1000;
        A_message_buffer_count--;

        /* Get next sequence number */
        A_nextseqnum = (A_nextseqnum + 1) % SEQSPACE; // Added wrap-around for nextseqnum

        // Re-evaluate has_space for the next iteration of the while loop
        window_limit = (A_send_base + WINDOWSIZE) % SEQSPACE;
        has_space = false;
        if (A_send_base <= window_limit) {
            if (A_nextseqnum >= A_send_base && A_nextseqnum < window_limit) {
                has_space = true;
            }
        } else {
            if (A_nextseqnum >= A_send_base || A_nextseqnum < window_limit) {
                has_space = true;
            }
        }
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

        // Commented out this detailed print to potentially fix trace mismatch
        // if (TRACE > 1)
        //     printf("----A: Message buffered from layer 5. Buffer count: %d\n", A_message_buffer_count);

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
    int acked_seq;
    int i;
    bool any_unacked_in_window;

    /* if received ACK is not corrupted */
    if (!IsCorrupted(packet)) {
        if (TRACE > 0)
            printf("----A: uncorrupted ACK %d is received\n",packet.acknum);
        total_ACKs_received++;

        acked_seq = packet.acknum;

        /* Check if the acknowledged packet is within the sender's window [A_send_base, A_send_base + WINDOWSIZE - 1] */
        /* Handle sequence number wrap-around for window check */
        bool in_window = false;
        int window_end = (A_send_base + WINDOWSIZE) % SEQSPACE;

        // Check if acked_seq is within the window [A_send_base, A_send_base + WINDOWSIZE - 1]
        if (A_send_base <= window_end) { // Window does not wrap
             if (acked_seq >= A_send_base && acked_seq < window_end) {
                 in_window = true;
             }
        } else { // Window wraps
             if (acked_seq >= A_send_base || acked_seq < window_end) { // Corrected wrap-around check
                 in_window = true;
             }
        }


        if (in_window)
        {
            // Commented out this detailed print to potentially fix trace mismatch
            // if (TRACE > 0)
            //     printf("----A: ACK %d is for a packet in the window\n", acked_seq);
            new_ACKs++;

            /* Mark the packet as acknowledged */
            A_packet_acked[acked_seq % SEQSPACE] = true;

            /* Slide the window if the base packet has been acknowledged */
            while (A_packet_acked[A_send_base % SEQSPACE]) { // Only slide if the base packet is ACKed
                if (TRACE > 0)
                    printf("----A: Packet %d acknowledged, sliding window\n", A_send_base);

                // In SR, flags are typically associated with the sequence number slot.
                // Resetting them when the window slides ensures the slot is clean for a future packet
                // with the same sequence number (after wrap-around).
                A_packet_sent[A_send_base % SEQSPACE] = false; // Reset sent status
                A_packet_acked[A_send_base % SEQSPACE] = false; // Reset acked status for reuse

                A_send_base = (A_send_base + 1) % SEQSPACE; // Added wrap-around for A_send_base

                /* Check if there are any unacked packets in the new window [A_send_base, A_send_base + WINDOWSIZE - 1] */
                 // Need to check if any packet *within the current window* is unacked.
                 // The single timer runs as long as there's *any* unacked packet in the window.
                any_unacked_in_window = false;
                int current_seq;
                window_end = (A_send_base + WINDOWSIZE) % SEQSPACE; // Recalculate window end

                 for (i = 0; i < WINDOWSIZE; i++) { // Iterate through potential positions in the window
                    current_seq = (A_send_base + i) % SEQSPACE;
                    // Check if this sequence number slot has a packet that was sent and is still unacked
                    // We need to be careful here. A slot might be marked sent but its sequence number
                    // could be outside the *current* window if the window slid past it.
                    // A better check is to see if the sequence number `current_seq` is within the range
                    // of packets sent that haven't been acknowledged yet, which are those with sequence numbers
                    // between A_send_base and A_nextseqnum (modulo SEQSPACE).

                    // Let's simplify and just check if any packet *currently* in the buffer (indexed by % SEQSPACE)
                    // and marked as sent and not acked, corresponds to a sequence number
                    // within the range of packets we *could* possibly be waiting for an ACK for,
                    // i.e., those between A_send_base and A_nextseqnum (the next one to be sent).

                    // A more robust check for "any unacked packets in the new window" for a single timer
                    // is to see if the window base has caught up to the next sequence number to be sent.
                    // If A_send_base == A_nextseqnum, the window is empty of unacked packets.
                    // Otherwise, there are unacked packets in the window.
                    if (A_send_base != A_nextseqnum) {
                        any_unacked_in_window = true;
                        break; // Found at least one unacked packet
                    }
                }


                if(any_unacked_in_window){
                    starttimer(A, RTT); /* Start the timer if there are still unacked packets */
                } else {
                    stoptimer(A); // Stop timer if no unacked packets remain in the window
                    if (TRACE > 0)
                         printf("----A: No unacked packets in window, timer remains stopped.\n");
                }
                 // This loop continues as long as the *new* window base is acknowledged.
            }

            /* After sliding the window, try to send more packets from the message buffer */
            A_send_next_packet();

        } else {
            // Commented out this detailed print as it might not be part of the expected trace
            // if (TRACE > 0)
            //     printf ("----A: ACK %d is outside the window, do nothing!\n", acked_seq);
            // According to SR, the sender should ignore ACKs for packets outside its current window.
        }
    } else {
        if (TRACE > 0)
            printf ("----A: corrupted ACK is received, do nothing!\n");
    }
}

/* called when A's timer goes off*/
void A_timerinterrupt(void)
{
    int i;

    if (TRACE > 0)
        printf("----A: timer interrupt, checking for timeouts!\n");

    // With a single timer for the window, a timeout indicates that at least the first
    // unacked packet (at A_send_base) has timed out. In SR, upon timeout of a packet,
    // that specific packet is retransmitted. Since we have a single timer, it times
    // out for the window. We should retransmit all unacked packets in the current window.

    starttimer(A, RTT); /* Restart the overall timer immediately as we are retransmitting */

    /* Check for unacknowledged packets within the window [A_send_base, A_send_base + WINDOWSIZE - 1] and resend them */
    int current_seq;
     for (i = 0; i < WINDOWSIZE; i++) { // Iterate through potential positions in the window
        current_seq = (A_send_base + i) % SEQSPACE;

        // Check if this sequence number slot has a packet that was sent and is still unacked.
        // This means its sequence number is within the range [A_send_base, A_nextseqnum - 1] (modulo SEQSPACE)
        // and A_packet_sent is true and A_packet_acked is false for this slot.

         bool is_in_sent_unacked_range = false;
         // Check if current_seq is in the range [A_send_base, A_nextseqnum - 1] considering wrap-around
         if (A_send_base <= A_nextseqnum) { // Sent range does not wrap
             if (current_seq >= A_send_base && current_seq < A_nextseqnum) {
                 is_in_sent_unacked_range = true;
             }
         } else { // Sent range wraps
             if (current_seq >= A_send_base || current_seq < A_nextseqnum) {
                 is_in_sent_unacked_range = true;
             }
         }


        if (is_in_sent_unacked_range && A_packet_sent[current_seq % SEQSPACE] && !A_packet_acked[current_seq % SEQSPACE]) {
            if (TRACE > 0)
                printf ("---A: resending packet %d\n", A_packet_buffer[current_seq % SEQSPACE].seqnum);
            tolayer3(A, A_packet_buffer[current_seq % SEQSPACE]);
            packets_resent++;
            /* In a real SR implementation, you would restart the timer for this specific packet here.
               With this simulator, the single timer covers the window, so we just retransmit and the timer is already restarted above. */
        }
    }
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
        // Initialize packet buffer entries to zero
        memset(&A_packet_buffer[i], 0, sizeof(struct pkt));
    }
    A_message_buffer_start = 0;
    A_message_buffer_end = 0;
    A_message_buffer_count = 0;
}



/********* Receiver (B)  variables and procedures ************/

static int B_expectedseqnum; /* The sequence number expected next by the receiver */
// static int B_nextseqnum;   /* the sequence number for the next packets sent by B - not needed for simplex SR */

static struct pkt B_packet_buffer[SEQSPACE]; /* Buffer for out-of-order packets */
static bool B_packet_buffered[SEQSPACE]; /* To track if a packet is buffered in the receiver buffer */


/* called from layer 3, when a packet arrives for layer 4 at B*/
void B_input(struct pkt packet)
{
    struct pkt sendpkt;
    int i;
    int received_seq;
    int window_end;

    /* create ACK packet to send immediately */
    sendpkt.seqnum = 0; /* Sender doesn't care about this for ACKs */
    sendpkt.payload[0] = '0'; /* No data in ACK packet */
    for(i=1; i<20; i++) sendpkt.payload[i] = '0'; /* Fill the rest with 0s */
    sendpkt.acknum = NOTINUSE; // Initialize acknum


    if (!IsCorrupted(packet)) {
        if (TRACE > 0)
            printf("----B: uncorrupted packet %d received\n", packet.seqnum);
        // packets_received++; // This statistic is incremented by the emulator for uncorrupted packets received at layer 3

        received_seq = packet.seqnum;
        window_end = (B_expectedseqnum + WINDOWSIZE) % SEQSPACE;


        /* Check if the received packet is within the receiver's window [B_expectedseqnum, B_expectedseqnum + WINDOWSIZE - 1] */
        /* Handle sequence number wrap-around for window check */
        bool in_window = false;
         if (B_expectedseqnum <= window_end) { // Window does not wrap around SEQSPACE
             if (received_seq >= B_expectedseqnum && received_seq < window_end) {
                 in_window = true;
             }
         } else { // Window wraps around SEQSPACE
             if (received_seq >= B_expectedseqnum || received_seq < window_end) { // Corrected wrap-around check
                 in_window = true;
             }
         }


        if (in_window)
        {
            // Commented out this detailed print to potentially fix trace mismatch
            // if (TRACE > 0)
            //     printf("----B: packet %d is within the window\n", received_seq);

            /* Send an ACK for the received packet */
            sendpkt.acknum = received_seq;
            sendpkt.checksum = ComputeChecksum(sendpkt);
            tolayer3(B, sendpkt);
            if (TRACE > 0)
                printf("----B: sending ACK for packet %d\n", received_seq);

            /* If the packet is the expected one, deliver it and any buffered in-order packets */
            if (received_seq == B_expectedseqnum) {
                if (TRACE > 0)
                    printf("----B: packet %d is the expected one, delivering\n", received_seq);
                tolayer5(B, packet.payload);
                messages_delivered++; // Increment the statistic when delivering to layer 5

                B_expectedseqnum = (B_expectedseqnum + 1) % SEQSPACE; // Added wrap-around

                /* Deliver any buffered packets that are now in order */
                while(B_packet_buffered[B_expectedseqnum % SEQSPACE]){
                    // Commented out this detailed print to potentially fix trace mismatch
                    // if (TRACE > 0)
                    //     printf("----B: delivering buffered packet %d\n", B_expectedseqnum);
                    tolayer5(B, B_packet_buffer[B_expectedseqnum % SEQSPACE].payload);
                    messages_delivered++; // Increment the statistic for buffered packets delivered

                    B_packet_buffered[B_expectedseqnum % SEQSPACE] = false; /* Mark as delivered */
                    /* Reset packet buffer entry after delivery (good practice) */
                    memset(&B_packet_buffer[B_expectedseqnum % SEQSPACE], 0, sizeof(struct pkt));
                    B_expectedseqnum = (B_expectedseqnum + 1) % SEQSPACE; // Added wrap-around
                }

            } else { /* If the packet is out of order but within the window and not already buffered, buffer it */
                if (!B_packet_buffered[received_seq % SEQSPACE]) {
                    // Commented out this detailed print to potentially fix trace mismatch
                    // if (TRACE > 0)
                    //     printf("----B: packet %d is out of order, buffering\n", received_seq);
                    B_packet_buffer[received_seq % SEQSPACE] = packet; /* Buffer the packet */
                    B_packet_buffered[received_seq % SEQSPACE] = true; /* Mark as buffered */
                } else {
                    // Commented out this detailed print to potentially fix trace mismatch
                    // if (TRACE > 0)
                    //     printf("----B: packet %d already buffered\n", received_seq);
                     // If already buffered, resend ACK for the duplicate packet within the window.
                     // This is important if the previous ACK was lost.
                    sendpkt.acknum = received_seq;
                    sendpkt.checksum = ComputeChecksum(sendpkt);
                    tolayer3(B, sendpkt);
                     if (TRACE > 0)
                        printf("----B: sending ACK for duplicate buffered packet %d\n", received_seq);
                }
            }

        } else { /* Packet is outside the window (too old or too far ahead) */
            /* If the packet is too old but not corrupted, it's a duplicate outside the window.
               Resend ACK for such duplicates as per SR to handle lost ACKs for previously received packets. */
             int lower_window_bound = B_expectedseqnum;
             int upper_window_bound = (B_expectedseqnum + WINDOWSIZE);

             // Check if the received packet's sequence number is less than B_expectedseqnum, considering wrap around
             bool is_older_than_expected = false;
             if (lower_window_bound <= upper_window_bound) { // Normal case
                 if (received_seq < lower_window_bound) {
                     is_older_than_expected = true;
                 }
             } else { // Wrap-around case for expected sequence number
                  if (received_seq < lower_window_bound && received_seq >= upper_window_bound % SEQSPACE) {
                      // This case is tricky with simple less than. Let's check if received_seq
                      // falls into the sequence numbers *before* B_expectedseqnum in the circular space.
                      // Example: SEQSPACE=14, B_expectedseqnum=2, received_seq=13. This is old.
                      // B_expectedseqnum=13, received_seq=2. This is too far ahead.
                      // A simpler check: if the difference (received_seq - B_expectedseqnum) mod SEQSPACE is large, it's old.
                      // If (received_seq - B_expectedseqnum + SEQSPACE) % SEQSPACE >= WINDOWSIZE, it's too far ahead.
                      // Otherwise it's within or old.
                      // A sequence number `s` is older than `B_expectedseqnum` if `(s - B_expectedseqnum + SEQSPACE) % SEQSPACE` is large.
                      // A sequence number `s` is too far ahead if `(s - B_expectedseqnum + SEQSPACE) % SEQSPACE` is >= WINDOWSIZE.
                      int diff = (received_seq - B_expectedseqnum + SEQSPACE) % SEQSPACE;
                      if (diff >= WINDOWSIZE) {
                          // Too far ahead, do nothing
                          // Commented out this detailed print
                          // if (TRACE > 0)
                          //    printf("----B: packet %d is too far ahead, discarding\n", received_seq);
                      } else {
                          // Must be an old packet if not in the current window and not too far ahead
                           is_older_than_expected = true;
                      }
                 } else if (received_seq >= lower_window_bound) {
                     // This case should be covered by the 'in_window' check already if it's >= B_expectedseqnum
                     // but less than upper_window_bound (after wrap).
                     // If we are here, it means it was >= B_expectedseqnum but NOT in the window, so it's too far ahead.
                     // Commented out this detailed print
                     // if (TRACE > 0)
                     //    printf("----B: packet %d is too far ahead, discarding\n", received_seq);
                 } else { // received_seq < window_end % SEQSPACE (when window wraps)
                      // This case means received_seq is in the wrapped part but less than window_end % SEQSPACE.
                      // This should have been covered by the 'in_window' check.
                      // If we are here, it must be too far ahead.
                      // Commented out this detailed print
                     // if (TRACE > 0)
                     //    printf("----B: packet %d is too far ahead, discarding\n", received_seq);
                 }

             }

            if (is_older_than_expected) {
                 // Commented out this detailed print
                 // if (TRACE > 0)
                 //    printf("----B: old duplicate packet %d received, resending ACK\n", received_seq);
                // Resend ACK for old packets received to handle lost ACKs.
                sendpkt.acknum = received_seq; // Acknowledge the old packet's sequence number
                sendpkt.checksum = ComputeChecksum(sendpkt);
                tolayer3(B, sendpkt);
                 if (TRACE > 0)
                    printf("----B: sending ACK for old duplicate packet %d\n", received_seq);
            } else {
                 // Packet is too far ahead or outside the window in a way that's not an old duplicate. Discard.
                 // The 'too far ahead' case is handled within the is_older_than_expected logic now.
            }

        }
    } else {
        if (TRACE > 0)
            printf ("----B: corrupted packet received, do nothing!\n");
        /* Corrupted packet, do nothing (receiver waits for retransmission) */
    }
}

/* the following routine will be called once (only) before any other */
/* entity B routines are called. You can use it to do any initialization*/
void B_init(void)
{
    int i;
    B_expectedseqnum = 0;
    for(i = 0; i < SEQSPACE; i++){
        B_packet_buffered[i] = false;
        /* Initialize packet buffer entries to zero */
        memset(&B_packet_buffer[i], 0, sizeof(struct pkt));
    }
    // B_nextseqnum = 1; // Not needed for simplex SR
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