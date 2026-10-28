/**
 * @file test_zlib.c
 * @brief Compatibility with zlib in both directions: what zlib writes, the decoder reads - at every level and with
 *        every strategy; what the encoder writes, zlib reads
 */
#include <string.h>
#include <zlib.h>
#include "dmcomp.h"
#include "test_util.h"

static const int g_strategies[] = { Z_DEFAULT_STRATEGY, Z_FILTERED, Z_HUFFMAN_ONLY, Z_RLE, Z_FIXED };

/** zlib's raw deflate; returns the size (0 on an error) */
static size_t zlib_deflate( const uint8_t* data, size_t size, uint8_t* out, size_t capacity, int level, int strategy )
{
    z_stream s;
    size_t result = 0;

    memset( &s, 0, sizeof(s) );
    if( deflateInit2( &s, level, Z_DEFLATED, -15, 9, strategy ) != Z_OK )
    {
        return 0;
    }
    s.next_in   = (Bytef*)data;
    s.avail_in  = (uInt)size;
    s.next_out  = out;
    s.avail_out = (uInt)capacity;
    if( deflate( &s, Z_FINISH ) == Z_STREAM_END )
    {
        result = s.total_out;
    }
    deflateEnd( &s );
    return result;
}

/** zlib's raw inflate; returns the size or -1 */
static long zlib_inflate( const uint8_t* stream, size_t size, uint8_t* out, size_t capacity )
{
    z_stream s;
    long result = -1;

    memset( &s, 0, sizeof(s) );
    if( inflateInit2( &s, -15 ) != Z_OK )
    {
        return -1;
    }
    s.next_in   = (Bytef*)stream;
    s.avail_in  = (uInt)size;
    s.next_out  = out;
    s.avail_out = (uInt)capacity;
    if( inflate( &s, Z_FINISH ) == Z_STREAM_END )
    {
        result = (long)s.total_out;
    }
    inflateEnd( &s );
    return result;
}

static void test_we_read_what_zlib_writes( void )
{
    static const size_t sizes[] = { 0, 1, 3, 100, 1000, 32768, 32769, 70000, 250000 };
    unsigned kind, s, st;
    int level;

    for( kind = 0; kind < DATA_KINDS; kind++ )
    {
        for( s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++ )
        {
            size_t size = sizes[s];
            // zlib's own bound is not one for Z_FILTERED and Z_RLE on random data: they write more than that
            size_t capacity = compressBound( (uLong)size ) + size / 8 + 1024;
            uint8_t* data = (uint8_t*)malloc( size ? size : 1 );
            uint8_t* packed = (uint8_t*)malloc( capacity );

            rng_seed( 100 + kind * 31 + s );
            fill_data( (data_kind_t)kind, data, size );
            for( level = 0; level <= 9; level++ )
            {
                for( st = 0; st < sizeof(g_strategies) / sizeof(g_strategies[0]); st++ )
                {
                    size_t packed_size, got = 0;
                    uint8_t* packed_exact;
                    uint8_t* out;
                    int result;

                    // The strategies are only worth the time with a few levels
                    if( st > 0 && level != 1 && level != 6 && level != 9 )
                    {
                        continue;
                    }
                    packed_size = zlib_deflate( data, size, packed, capacity, level, g_strategies[st] );
                    CHECK( packed_size > 0 );
                    packed_exact = exact_copy( packed, packed_size );
                    out = (uint8_t*)malloc( size ? size : 1 );
                    result = dmcomp_inflate( packed_exact, packed_size, out, size, &got );
                    CHECK_EQ( result, DMCOMP_OK );
                    CHECK_EQ( got, size );
                    if( result != DMCOMP_OK || got != size || (size && memcmp( out, data, size ) != 0) )
                    {
                        g_failures++;
                        printf( "  FAIL zlib stream: %s, %zu bytes, level %d, strategy %d\n",
                                data_name( (data_kind_t)kind ), size, level, g_strategies[st] );
                    }
                    free( out );
                    free( packed_exact );
                }
            }
            free( packed );
            free( data );
        }
    }
}

static void test_zlib_reads_what_we_write( void )
{
    static const size_t sizes[] = { 0, 1, 3, 100, 1000, 32768, 32769, 65536, 70000, 250000 };
    unsigned kind, s;
    int level;

    for( kind = 0; kind < DATA_KINDS; kind++ )
    {
        for( s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++ )
        {
            size_t size = sizes[s];
            uint8_t* data = (uint8_t*)malloc( size ? size : 1 );
            uint8_t* packed = (uint8_t*)malloc( dmcomp_deflate_bound( size ) );
            uint8_t* out = (uint8_t*)malloc( size ? size : 1 );

            rng_seed( 200 + kind * 31 + s );
            fill_data( (data_kind_t)kind, data, size );
            for( level = 0; level <= 9; level++ )
            {
                size_t packed_size = 0;
                long got;

                CHECK_EQ( dmcomp_deflate( data, size, packed, dmcomp_deflate_bound( size ), &packed_size, level ),
                          DMCOMP_OK );
                got = zlib_inflate( packed, packed_size, out, size );
                CHECK_EQ( got, (long)size );
                if( got != (long)size || (size && memcmp( out, data, size ) != 0) )
                {
                    g_failures++;
                    printf( "  FAIL zlib does not read: %s, %zu bytes, level %d\n",
                            data_name( (data_kind_t)kind ), size, level );
                }
            }
            free( out );
            free( packed );
            free( data );
        }
    }
}

static void test_ratio_against_zlib( void )
{
    // Not a contest: the encoder has to stay in the neighbourhood of zlib's, or something went wrong with the matching
    // or the codes. (The same level means about the same effort here.)
    static const data_kind_t kinds[] = { DATA_TEXT, DATA_CODE, DATA_PATTERN, DATA_RUNS, DATA_MIXED };
    size_t size = 200000;
    uint8_t* data = (uint8_t*)malloc( size );
    uint8_t* ours = (uint8_t*)malloc( dmcomp_deflate_bound( size ) );
    uint8_t* theirs = (uint8_t*)malloc( compressBound( (uLong)size ) );
    unsigned k;
    int level;

    for( k = 0; k < sizeof(kinds) / sizeof(kinds[0]); k++ )
    {
        rng_seed( 300 + k );
        fill_data( kinds[k], data, size );
        for( level = 1; level <= 9; level += 4 )
        {
            size_t our_size = 0;
            size_t zlib_size = zlib_deflate( data, size, theirs, compressBound( (uLong)size ), level, Z_DEFAULT_STRATEGY );

            CHECK_EQ( dmcomp_deflate( data, size, ours, dmcomp_deflate_bound( size ), &our_size, level ), DMCOMP_OK );
            printf( "  %-8s level %d: ours %6zu, zlib %6zu\n", data_name( kinds[k] ), level, our_size, zlib_size );
            CHECK( our_size <= zlib_size + zlib_size / 8 + 64 );
        }
    }
    free( theirs );
    free( ours );
    free( data );
}

int main( void )
{
    RUN( test_we_read_what_zlib_writes );
    RUN( test_zlib_reads_what_we_write );
    RUN( test_ratio_against_zlib );
    return finish( "test_zlib" );
}
