/**
 * @file test_roundtrip.c
 * @brief The encoder and the decoder together: everything that goes in comes out, in buffers of exactly the right size
 */
#include <string.h>
#include "dmcomp.h"
#include "test_util.h"

/**
 * Compresses and decompresses, with every buffer a block of exactly the size that the call may use (so AddressSanitizer
 * sees any access outside), and checks the result.
 *
 * @return size of the compressed data, 0 on a failure
 */
static size_t roundtrip( const uint8_t* data, size_t size, int level, const char* what )
{
    size_t   bound = dmcomp_deflate_bound( size );
    uint8_t* packed = (uint8_t*)malloc( bound );
    uint8_t* unpacked;
    uint8_t* packed_exact;
    size_t   packed_size = 0, unpacked_size = 0;
    int      result;

    result = dmcomp_deflate( data, size, packed, bound, &packed_size, level );
    CHECK_EQ( result, DMCOMP_OK );
    CHECK( packed_size <= bound );
    if( result != DMCOMP_OK || packed_size > bound )
    {
        printf( "  (%s, %zu bytes, level %d: %s)\n", what, size, level, dmcomp_strerror( result ) );
        free( packed );
        return 0;
    }

    packed_exact = exact_copy( packed, packed_size );
    unpacked = (uint8_t*)malloc( size ? size : 1 );
    result = dmcomp_inflate( packed_exact, packed_size, unpacked, size, &unpacked_size );
    CHECK_EQ( result, DMCOMP_OK );
    CHECK_EQ( unpacked_size, size );
    if( result != DMCOMP_OK || unpacked_size != size || (size && memcmp( unpacked, data, size ) != 0) )
    {
        g_failures++;
        printf( "  FAIL round trip (%s, %zu bytes, level %d: %s)\n", what, size, level, dmcomp_strerror( result ) );
        packed_size = 0;
    }

    free( packed );
    free( packed_exact );
    free( unpacked );
    return packed_size;
}

static void test_all_kinds_sizes_levels( void )
{
    static const size_t sizes[] =
    {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 31, 32, 33, 100, 257, 258, 259, 260, 1000, 4095, 4096, 4097, 20000,
        32766, 32767, 32768, 32769, 40000, 65534, 65535, 65536, 65537, 70000
    };
    static const size_t big_sizes[] = { 131072, 200003, 400000 };
    unsigned kind, s;
    int level;

    for( kind = 0; kind < DATA_KINDS; kind++ )
    {
        for( s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++ )
        {
            uint8_t* data = (uint8_t*)malloc( sizes[s] ? sizes[s] : 1 );

            rng_seed( 1000 + kind * 100 + s );
            fill_data( (data_kind_t)kind, data, sizes[s] );
            for( level = 0; level <= 9; level++ )
            {
                roundtrip( data, sizes[s], level, data_name( (data_kind_t)kind ) );
            }
            free( data );
        }
        for( s = 0; s < sizeof(big_sizes) / sizeof(big_sizes[0]); s++ )
        {
            uint8_t* data = (uint8_t*)malloc( big_sizes[s] );
            static const int levels[] = { 0, 1, 6, 9 };
            unsigned l;

            rng_seed( 5000 + kind * 100 + s );
            fill_data( (data_kind_t)kind, data, big_sizes[s] );
            for( l = 0; l < sizeof(levels) / sizeof(levels[0]); l++ )
            {
                roundtrip( data, big_sizes[s], levels[l], data_name( (data_kind_t)kind ) );
            }
            free( data );
        }
    }
}

static void test_many_blocks( void )
{
    // Every 16384 tokens is a block of its own: a megabyte of text is dozens of them, a megabyte of zeros is two
    size_t size = 1u << 20;
    uint8_t* data = (uint8_t*)malloc( size );
    size_t packed;

    rng_seed( 77 );
    fill_data( DATA_TEXT, data, size );
    packed = roundtrip( data, size, 6, "megabyte of text" );
    CHECK( packed > 0 && packed < size / 3 );

    fill_data( DATA_ZEROS, data, size );
    packed = roundtrip( data, size, 6, "megabyte of zeros" );
    CHECK( packed > 0 && packed < 4096 );

    fill_data( DATA_MIXED, data, size );
    roundtrip( data, size, 9, "megabyte of everything" );
    free( data );
}

static void test_long_codes_are_limited( void )
{
    // The byte i appears fib(i) times: the optimal Huffman code is as deep as the alphabet is big (24 here), the
    // format allows 15 bits - the encoder has to cut the tree and still write a valid, complete code
    size_t counts[26], total = 0, size, i, k = 0;
    unsigned symbols = 25, s;
    uint8_t* data;

    counts[0] = 1;
    counts[1] = 1;
    for( s = 2; s < symbols; s++ ) counts[s] = counts[s - 1] + counts[s - 2];
    for( s = 0; s < symbols; s++ ) total += counts[s];
    size = total;
    data = (uint8_t*)malloc( size );
    for( s = 0; s < symbols; s++ )
    {
        for( i = 0; i < counts[s]; i++ ) data[k++] = (uint8_t)(s * 9 + 1);
    }
    // Shuffled, so the runs do not turn into matches and the literals keep their frequencies
    rng_seed( 4242 );
    for( i = size - 1; i > 0; i-- )
    {
        size_t j = rng_below( (uint32_t)(i + 1) );
        uint8_t tmp = data[i]; data[i] = data[j]; data[j] = tmp;
    }
    for( s = 1; s <= 9; s++ )
    {
        roundtrip( data, size, (int)s, "fibonacci" );
    }
    free( data );
}

static void test_compression_is_effective( void )
{
    size_t size = 100000, level1, level6, level9;
    uint8_t* data = (uint8_t*)malloc( size );

    rng_seed( 9 );
    fill_data( DATA_ZEROS, data, size );
    CHECK( roundtrip( data, size, 6, "zeros" ) < 200 );

    fill_data( DATA_TEXT, data, size );
    level1 = roundtrip( data, size, 1, "text" );
    level6 = roundtrip( data, size, 6, "text" );
    level9 = roundtrip( data, size, 9, "text" );
    CHECK( level1 > 0 && level6 > 0 && level9 > 0 );
    CHECK( level6 < size / 3 );
    CHECK( level6 <= level1 );                  // More effort never costs here
    CHECK( level9 <= level6 + level6 / 100 );

    fill_data( DATA_PATTERN, data, size );
    CHECK( roundtrip( data, size, 6, "pattern" ) < 2000 );

    fill_data( DATA_CODE, data, size );
    CHECK( roundtrip( data, size, 6, "code" ) < size / 2 );
    free( data );
}

static void test_longest_distance( void )
{
    // 32767 random bytes, then the same ones again: the second half is one match at the longest distance the
    // encoder uses (and a few more for the 258 byte limit of a match)
    size_t size = 2 * 32767;
    uint8_t* data = (uint8_t*)malloc( size );
    size_t packed;

    rng_seed( 31 );
    fill_data( DATA_FAR_REPEAT, data, size );
    packed = roundtrip( data, size, 6, "far repeat" );
    CHECK( packed > 32767 && packed < 32767 + 1500 );
    free( data );
}

static void test_random_data_is_stored( void )
{
    size_t size = 150000, packed;
    uint8_t* data = (uint8_t*)malloc( size );
    int level;

    rng_seed( 3 );
    fill_data( DATA_RANDOM, data, size );
    for( level = 1; level <= 9; level++ )
    {
        packed = roundtrip( data, size, level, "random" );
        CHECK( packed > 0 && packed <= size + 6 * (size / 16384 + 1) );     // Stored blocks, hardly any bigger
    }
    CHECK_EQ( roundtrip( data, size, 0, "random" ), size + 5 * 3 );         // 150000 = 2 x 65535 + 18930 -> 3 headers
    free( data );
}

static void test_output_buffer_size( void )
{
    size_t size = 5000, packed_size = 0, cap;
    uint8_t* data = (uint8_t*)malloc( size );
    uint8_t* big = (uint8_t*)malloc( dmcomp_deflate_bound( size ) );

    rng_seed( 12 );
    fill_data( DATA_TEXT, data, size );
    CHECK_EQ( dmcomp_deflate( data, size, big, dmcomp_deflate_bound( size ), &packed_size, 6 ), DMCOMP_OK );

    // Exactly enough is enough, one byte less is not - and nothing is written beyond the buffer either way
    for( cap = packed_size - 1; cap <= packed_size + 3; cap++ )
    {
        uint8_t* exact = (uint8_t*)malloc( cap );
        size_t got = 0;
        int result = dmcomp_deflate( data, size, exact, cap, &got, 6 );

        if( cap < packed_size )
        {
            CHECK_EQ( result, DMCOMP_ERR_OVERFLOW );
        }
        else
        {
            CHECK_EQ( result, DMCOMP_OK );
            CHECK_EQ( got, packed_size );
            CHECK( memcmp( exact, big, packed_size ) == 0 );
        }
        free( exact );
    }
    for( cap = 0; cap < 40; cap++ )
    {
        uint8_t* exact = (uint8_t*)malloc( cap ? cap : 1 );
        size_t got = 0;
        CHECK_EQ( dmcomp_deflate( data, size, exact, cap, &got, 6 ), DMCOMP_ERR_OVERFLOW );
        CHECK_EQ( dmcomp_deflate( data, size, exact, cap, &got, 0 ), DMCOMP_ERR_OVERFLOW );
        free( exact );
    }
    free( big );
    free( data );
}

static void test_parameters( void )
{
    uint8_t in[10] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 }, out[64], back[10];
    size_t size = 0;
    int level;

    CHECK_EQ( dmcomp_deflate( in, sizeof(in), out, sizeof(out), NULL, 6 ), DMCOMP_ERR_PARAM );
    CHECK_EQ( dmcomp_deflate( NULL, 4, out, sizeof(out), &size, 6 ), DMCOMP_ERR_PARAM );
    CHECK_EQ( dmcomp_deflate( in, sizeof(in), NULL, 4, &size, 6 ), DMCOMP_ERR_PARAM );

    // A level out of range is not an error
    for( level = -5; level <= 12; level++ )
    {
        size_t got = 0, unpacked = 0;
        CHECK_EQ( dmcomp_deflate( in, sizeof(in), out, sizeof(out), &size, level ), DMCOMP_OK );
        CHECK_EQ( dmcomp_inflate( out, size, back, sizeof(back), &got ), DMCOMP_OK );
        CHECK_EQ( got, sizeof(in) );
        CHECK( memcmp( back, in, sizeof(in) ) == 0 );
        (void)unpacked;
    }

    // Nothing in, nothing (but the end of the stream) out; NULL is fine for an empty input
    CHECK_EQ( dmcomp_deflate( NULL, 0, out, sizeof(out), &size, 6 ), DMCOMP_OK );
    CHECK_EQ( dmcomp_inflate( out, size, back, 0, &size ), DMCOMP_OK );
    CHECK_EQ( size, 0 );
}

static void test_deterministic( void )
{
    size_t size = 70000, a_size = 0, b_size = 0;
    uint8_t* data = (uint8_t*)malloc( size );
    uint8_t* a = (uint8_t*)malloc( dmcomp_deflate_bound( size ) );
    uint8_t* b = (uint8_t*)malloc( dmcomp_deflate_bound( size ) );

    rng_seed( 8 );
    fill_data( DATA_MIXED, data, size );
    CHECK_EQ( dmcomp_deflate( data, size, a, dmcomp_deflate_bound( size ), &a_size, 6 ), DMCOMP_OK );
    CHECK_EQ( dmcomp_deflate( data, size, b, dmcomp_deflate_bound( size ), &b_size, 6 ), DMCOMP_OK );
    CHECK_EQ( a_size, b_size );
    CHECK( memcmp( a, b, a_size ) == 0 );
    free( a ); free( b ); free( data );
}

int main( void )
{
    RUN( test_parameters );
    RUN( test_deterministic );
    RUN( test_output_buffer_size );
    RUN( test_random_data_is_stored );
    RUN( test_longest_distance );
    RUN( test_compression_is_effective );
    RUN( test_long_codes_are_limited );
    RUN( test_many_blocks );
    RUN( test_all_kinds_sizes_levels );
    return finish( "test_roundtrip" );
}
