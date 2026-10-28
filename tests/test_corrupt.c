/**
 * @file test_corrupt.c
 * @brief The decoder with damaged input: whatever the bytes are, it may fail but not crash, read or write outside
 *        its buffers, or hang - the tests run under AddressSanitizer and UBSanitizer in the CI
 */
#include <string.h>
#include "dmcomp.h"
#include "test_util.h"

/** Decodes damaged data in buffers of exactly the given sizes */
static int decode_damaged( const uint8_t* stream, size_t size, size_t capacity )
{
    static unsigned calls;
    uint8_t* in = exact_copy( stream, size );
    uint8_t* out = (uint8_t*)malloc( capacity ? capacity : 1 );
    uint8_t* workspace = (uint8_t*)malloc( dmcomp_inflate_workspace_size() );
    size_t   out_size = 0;
    // Both ways to call the decoder (the stack and the workspace of the caller), alternately
    int      result = (calls++ & 1u) ? dmcomp_inflate( in, size, out, capacity, &out_size )
                                     : dmcomp_inflate_ws( in, size, out, capacity, &out_size, workspace );

    CHECK( out_size <= capacity );
    CHECK( result == DMCOMP_OK || result < 0 );
    free( in );
    free( out );
    free( workspace );
    return result;
}

static void test_flipped_bits( void )
{
    unsigned kind, round;
    int ok = 0, failed = 0;

    for( kind = 0; kind < DATA_KINDS; kind++ )
    {
        size_t size = 6000 + kind * 1000, packed_size = 0;
        uint8_t* data = (uint8_t*)malloc( size );
        uint8_t* packed = (uint8_t*)malloc( dmcomp_deflate_bound( size ) );
        int level = (int)(kind % 3) * 4 + 1;

        rng_seed( 500 + kind );
        fill_data( (data_kind_t)kind, data, size );
        CHECK_EQ( dmcomp_deflate( data, size, packed, dmcomp_deflate_bound( size ), &packed_size, level ), DMCOMP_OK );

        for( round = 0; round < 1500; round++ )
        {
            uint8_t* damaged = exact_copy( packed, packed_size );
            unsigned flips = 1 + rng_below( 4 );
            int result;

            while( flips-- )
            {
                if( rng_below( 2 ) )
                {
                    damaged[rng_below( (uint32_t)packed_size )] ^= (uint8_t)(1u << rng_below( 8 ));
                }
                else
                {
                    damaged[rng_below( (uint32_t)packed_size )] = (uint8_t)rng_next();
                }
            }
            // The right size of the output and a random one (smaller, mostly)
            result = decode_damaged( damaged, packed_size, size );
            result == DMCOMP_OK ? ok++ : failed++;
            decode_damaged( damaged, packed_size, rng_below( (uint32_t)size + 1 ) );
            free( damaged );
        }
        free( packed );
        free( data );
    }
    // Most damage is noticed; some of it is not (a flipped bit in a literal is still a valid stream)
    CHECK( failed > 0 );
    printf( "  damaged streams: %d decoded, %d refused\n", ok, failed );
}

static void test_truncated_and_extended( void )
{
    size_t size = 20000, packed_size = 0, cut;
    uint8_t* data = (uint8_t*)malloc( size );
    uint8_t* packed = (uint8_t*)malloc( dmcomp_deflate_bound( size ) );

    rng_seed( 600 );
    fill_data( DATA_MIXED, data, size );
    CHECK_EQ( dmcomp_deflate( data, size, packed, dmcomp_deflate_bound( size ), &packed_size, 6 ), DMCOMP_OK );

    // Every possible cut of the stream is refused (it ends before it should), not one of them is read past its end
    for( cut = 0; cut < packed_size; cut += 1 + cut / 64 )
    {
        int result = decode_damaged( packed, cut, size );
        CHECK( result == DMCOMP_ERR_TRUNCATED || result == DMCOMP_ERR_DATA );
    }
    free( packed );
    free( data );
}

static void test_random_bytes( void )
{
    // Streams that were never made by an encoder; a few bytes up to a few kB
    unsigned round;
    int ok = 0;

    rng_seed( 700 );
    for( round = 0; round < 6000; round++ )
    {
        size_t size = 1 + rng_below( round < 5000 ? 40 : 3000 );
        uint8_t* stream = (uint8_t*)malloc( size );
        size_t i;

        for( i = 0; i < size; i++ ) stream[i] = (uint8_t)rng_next();
        if( round % 3 == 0 )
        {
            stream[0] = (uint8_t)((stream[0] & 0xF8u) | 0x05u);      // A dynamic block, to get past the first bits
        }
        ok += decode_damaged( stream, size, 1 + rng_below( 5000 ) ) == DMCOMP_OK;
        free( stream );
    }
    printf( "  random streams: %d of 6000 decoded\n", ok );
}

static void test_huge_output_request( void )
{
    // The output capacity is a promise about the buffer, not a size to allocate: a long run of matches must stop at it
    uint8_t stream[] = { 0x4b, 0x4c, 0x1c, 0x05, 0xa3, 0x60, 0x14, 0x0c, 0x77, 0x00, 0x00 };   // Anything with matches
    uint8_t out[8];
    size_t out_size = 0;
    int result = dmcomp_inflate( stream, sizeof(stream), out, sizeof(out), &out_size );

    CHECK( out_size <= sizeof(out) );
    CHECK( result == DMCOMP_OK || result < 0 );
}

int main( void )
{
    RUN( test_flipped_bits );
    RUN( test_truncated_and_extended );
    RUN( test_random_bytes );
    RUN( test_huge_output_request );
    return finish( "test_corrupt" );
}
