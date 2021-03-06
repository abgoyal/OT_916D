

#include "main.h"
#include "bitarray.h"

uint8_t get_bit_status(TYPE_OF_WORD *seq_bits, uint16_t last_seqno,
		       uint16_t curr_seqno)
{
	int16_t diff, word_offset, word_num;

	diff = last_seqno - curr_seqno;
	if (diff < 0 || diff >= TQ_LOCAL_WINDOW_SIZE) {
		return 0;
	} else {
		/* which word */
		word_num = (last_seqno - curr_seqno) / WORD_BIT_SIZE;
		/* which position in the selected word */
		word_offset = (last_seqno - curr_seqno) % WORD_BIT_SIZE;

		if (seq_bits[word_num] & 1 << word_offset)
			return 1;
		else
			return 0;
	}
}

/* turn corresponding bit on, so we can remember that we got the packet */
void bit_mark(TYPE_OF_WORD *seq_bits, int32_t n)
{
	int32_t word_offset, word_num;

	/* if too old, just drop it */
	if (n < 0 || n >= TQ_LOCAL_WINDOW_SIZE)
		return;

	/* which word */
	word_num = n / WORD_BIT_SIZE;
	/* which position in the selected word */
	word_offset = n % WORD_BIT_SIZE;

	seq_bits[word_num] |= 1 << word_offset;	/* turn the position on */
}

/* shift the packet array by n places. */
void bit_shift(TYPE_OF_WORD *seq_bits, int32_t n)
{
	int32_t word_offset, word_num;
	int32_t i;

	if (n <= 0 || n >= TQ_LOCAL_WINDOW_SIZE)
		return;

	word_offset = n % WORD_BIT_SIZE;/* shift how much inside each word */
	word_num = n / WORD_BIT_SIZE;	/* shift over how much (full) words */

	for (i = NUM_WORDS - 1; i > word_num; i--) {
		/* going from old to new, so we don't overwrite the data we copy
		 * from.
		 *
		 * left is high, right is low: FEDC BA98 7654 3210
		 *					  ^^ ^^
		 *			       vvvv
		 * ^^^^ = from, vvvvv =to, we'd have word_num==1 and
		 * word_offset==WORD_BIT_SIZE/2 ????? in this example.
		 * (=24 bits)
		 *
		 * our desired output would be: 9876 5432 1000 0000
		 * */

		seq_bits[i] =
			(seq_bits[i - word_num] << word_offset) +
			/* take the lower port from the left half, shift it left
			 * to its final position */
			(seq_bits[i - word_num - 1] >>
			 (WORD_BIT_SIZE-word_offset));
		/* and the upper part of the right half and shift it left to
		 * it's position */
		/* for our example that would be: word[0] = 9800 + 0076 =
		 * 9876 */
	}
	/* now for our last word, i==word_num, we only have the it's "left"
	 * half. that's the 1000 word in our example.*/

	seq_bits[i] = (seq_bits[i - word_num] << word_offset);

	/* pad the rest with 0, if there is anything */
	i--;

	for (; i >= 0; i--)
		seq_bits[i] = 0;
}

static void bit_reset_window(TYPE_OF_WORD *seq_bits)
{
	int i;
	for (i = 0; i < NUM_WORDS; i++)
		seq_bits[i] = 0;
}


char bit_get_packet(TYPE_OF_WORD *seq_bits, int16_t seq_num_diff,
		    int8_t set_mark)
{
	/* sequence number is slightly older. We already got a sequence number
	 * higher than this one, so we just mark it. */

	if ((seq_num_diff <= 0) && (seq_num_diff > -TQ_LOCAL_WINDOW_SIZE)) {
		if (set_mark)
			bit_mark(seq_bits, -seq_num_diff);
		return 0;
	}

	/* sequence number is slightly newer, so we shift the window and
	 * set the mark if required */

	if ((seq_num_diff > 0) && (seq_num_diff < TQ_LOCAL_WINDOW_SIZE)) {
		bit_shift(seq_bits, seq_num_diff);

		if (set_mark)
			bit_mark(seq_bits, 0);
		return 1;
	}

	/* sequence number is much newer, probably missed a lot of packets */

	if ((seq_num_diff >= TQ_LOCAL_WINDOW_SIZE)
		|| (seq_num_diff < EXPECTED_SEQNO_RANGE)) {
		bat_dbg(DBG_BATMAN,
			"We missed a lot of packets (%i) !\n",
			seq_num_diff - 1);
		bit_reset_window(seq_bits);
		if (set_mark)
			bit_mark(seq_bits, 0);
		return 1;
	}

	/* received a much older packet. The other host either restarted
	 * or the old packet got delayed somewhere in the network. The
	 * packet should be dropped without calling this function if the
	 * seqno window is protected. */

	if ((seq_num_diff <= -TQ_LOCAL_WINDOW_SIZE)
		|| (seq_num_diff >= EXPECTED_SEQNO_RANGE)) {

		bat_dbg(DBG_BATMAN,
			"Other host probably restarted!\n");

		bit_reset_window(seq_bits);
		if (set_mark)
			bit_mark(seq_bits, 0);

		return 1;
	}

	/* never reached */
	return 0;
}

int bit_packet_count(TYPE_OF_WORD *seq_bits)
{
	int i, hamming = 0;
	TYPE_OF_WORD word;

	for (i = 0; i < NUM_WORDS; i++) {
		word = seq_bits[i];

		while (word) {
			word &= word-1;
			hamming++;
		}
	}
	return hamming;
}
