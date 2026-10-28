/**
 * @file test_huffman.c
 * @brief The length limited Huffman codes of the encoder, tested on their own (the encoder's source is included, so
 *        its internal functions are within reach)
 *
 * Whatever the frequencies, the lengths have to make a complete prefix code (the sum of 2^-length is exactly 1: an
 * incomplete or an over-subscribed code is refused by decoders), none longer than the limit, and a code of the
 * frequencies that need more bits than allowed has to be cut properly.
 */
#include "dmcomp_deflate.c"
#include "test_util.h"

/** Sum of 2^(15 - length) over the used symbols: 2^15 for a complete code */
static uint64_t kraft_sum( const uint8_t* lengths, unsigned symbols, unsigned* longest, unsigned* used )
{
    uint64_t sum = 0;
    unsigned i;

    *longest = 0;
    *used = 0;
    for( i = 0; i < symbols; i++ )
    {
        if( lengths[i] != 0 )
        {
            sum += 1ull << (DMCOMP_MAX_BITS - lengths[i]);
            (*used)++;
            if( lengths[i] > *longest )
            {
                *longest = lengths[i];
            }
        }
    }
    return sum;
}

static void test_random_frequencies( void )
{
    unsigned iteration;
    int bad = 0;

    rng_seed( 1 );
    for( iteration = 0; iteration < 100000; iteration++ )
    {
        unsigned limit = rng_below( 2 ) ? DMCOMP_MAX_BITS : DMCOMP_MAX_CLEN_BITS;
        unsigned symbols = 2 + rng_below( DMCOMP_LITLEN_SYMBOLS - 1 );
        unsigned mode = rng_below( 3 ), i, longest, used;
        uint32_t freq[DMCOMP_LITLEN_SYMBOLS];
        uint8_t  lengths[DMCOMP_LITLEN_SYMBOLS];

        if( symbols > (1u << limit) )
        {
            symbols = 1u << limit;
        }
        for( i = 0; i < symbols; i++ )
        {
            // Some symbols unused; the used ones from nearly equal to wildly different (powers of two: the deepest trees)
            freq[i] = rng_below( 3 ) == 0 ? 0 :
                      mode == 0 ? 1 + rng_below( 10 ) :
                      mode == 1 ? 1u << rng_below( 20 ) :
                                  1 + rng_below( 100000 );
        }
        build_lengths( freq, symbols, limit, lengths );
        if( kraft_sum( lengths, symbols, &longest, &used ) != (1ull << DMCOMP_MAX_BITS) || longest > limit || used < 2 )
        {
            bad++;
        }
        for( i = 0; i < symbols; i++ )
        {
            // A symbol that is used has a code
            if( freq[i] != 0 && lengths[i] == 0 )
            {
                bad++;
            }
        }
    }
    CHECK_EQ( bad, 0 );
}

static void test_deep_tree_is_cut( void )
{
    // Fibonacci frequencies make the deepest possible tree: 30 symbols need 29 bits, the format allows 15
    uint32_t freq[30], a = 1, b = 1;
    uint8_t lengths[30];
    unsigned i, longest, used;

    for( i = 0; i < 30; i++ )
    {
        freq[i] = a;
        b += a;
        a = b - a;
    }
    build_lengths( freq, 30, DMCOMP_MAX_BITS, lengths );
    CHECK_EQ( kraft_sum( lengths, 30, &longest, &used ), 1ull << DMCOMP_MAX_BITS );
    CHECK_EQ( longest, DMCOMP_MAX_BITS );
    CHECK_EQ( used, 30 );
    // The most frequent symbol keeps the shortest code
    CHECK( lengths[29] <= lengths[0] );
}

static void test_few_symbols( void )
{
    uint32_t freq[10] = { 0 };
    uint8_t lengths[10];
    unsigned longest, used;

    // Nothing used: two codes of one bit anyway; one used symbol: the same
    build_lengths( freq, 10, DMCOMP_MAX_BITS, lengths );
    CHECK_EQ( kraft_sum( lengths, 10, &longest, &used ), 1ull << DMCOMP_MAX_BITS );
    CHECK_EQ( used, 2 );
    freq[7] = 1000;
    build_lengths( freq, 10, DMCOMP_MAX_BITS, lengths );
    CHECK_EQ( kraft_sum( lengths, 10, &longest, &used ), 1ull << DMCOMP_MAX_BITS );
    CHECK_EQ( used, 2 );
    CHECK_EQ( lengths[7], 1 );
}

static void test_run_length_coding( void )
{
    // The code lengths of a header: 0 0 0 ... (40 zeros), 5 x 9, 3, 3, 3, 3, 3, 3, 3, 3, 3 (9 times)
    uint8_t lengths[300], symbols[300], extras[300], decoded[300];
    unsigned total = 0, count, i, k = 0;

    for( i = 0; i < 40; i++ ) lengths[total++] = 0;
    for( i = 0; i < 9; i++ )  lengths[total++] = 5;
    for( i = 0; i < 9; i++ )  lengths[total++] = 3;
    for( i = 0; i < 2; i++ )  lengths[total++] = 0;
    for( i = 0; i < 5; i++ )  lengths[total++] = 7;
    for( i = 0; i < 150; i++ ) lengths[total++] = 0;
    count = rle_lengths( lengths, total, symbols, extras );

    // Read it back the way a decoder does
    for( i = 0; i < count; i++ )
    {
        unsigned repeat = 1, value = symbols[i];
        if( symbols[i] == 16 )      { value = decoded[k - 1]; repeat = 3 + extras[i]; }
        else if( symbols[i] == 17 ) { value = 0; repeat = 3 + extras[i]; }
        else if( symbols[i] == 18 ) { value = 0; repeat = 11 + extras[i]; }
        while( repeat-- ) decoded[k++] = (uint8_t)value;
    }
    CHECK_EQ( k, total );
    CHECK( memcmp( decoded, lengths, total ) == 0 );
    CHECK( count < total / 4 );
}

int main( void )
{
    RUN( test_random_frequencies );
    RUN( test_deep_tree_is_cut );
    RUN( test_few_symbols );
    RUN( test_run_length_coding );
    return finish( "test_huffman" );
}
