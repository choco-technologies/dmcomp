/**
 * @file test_vectors.c
 * @brief The decoder against streams it did not make: zlib's, and hand made ones - valid and not
 */
#include <string.h>
#include "dmcomp.h"
#include "test_util.h"
#include "vectors.h"

#define PARAGRAPH   "DMOD modules are loaded from the external flash. The module loader reads the compressed file, " \
                    "unpacks it into the heap and relocates it. A compressed module is smaller, so it is read faster. " \
                    "DMOD modules are loaded from the external flash.\n"
static const char vec_text[] = PARAGRAPH PARAGRAPH PARAGRAPH;

typedef struct
{
    const char*     name;
    const uint8_t*  stream;
    size_t          stream_size;
    const void*     expected;
    size_t          expected_size;
} vector_t;

static const vector_t g_vectors[] =
{
    { "fixed a",        vec_fixed_a,        sizeof(vec_fixed_a),        "a",                              1 },
    { "fixed match",    vec_fixed_match,    sizeof(vec_fixed_match),    "abcabcabcabcabcabcabcabcabcabc", 30 },
    { "dynamic",        vec_dynamic,        sizeof(vec_dynamic),        vec_text,       sizeof(vec_text) - 1 },
    { "huffman only",   vec_huffman_only,   sizeof(vec_huffman_only),   vec_text,                       300 },
    { "stored",         vec_stored,         sizeof(vec_stored),         "stored data",                   11 },
};
#define VECTOR_COUNT    (sizeof(g_vectors) / sizeof(g_vectors[0]))

// ---------------------------------------------------------------------------------------------------------------------
//      Streams written by hand
// ---------------------------------------------------------------------------------------------------------------------

typedef struct
{
    uint8_t buf[1024];
    size_t  nbits;
} bits_t;

/** The fields of the format: the lowest bit goes first */
static void bits_put( bits_t* b, uint32_t value, unsigned count )
{
    unsigned i;
    for( i = 0; i < count; i++ )
    {
        if( b->nbits % 8 == 0 )
        {
            b->buf[b->nbits / 8] = 0;
        }
        b->buf[b->nbits / 8] |= (uint8_t)(((value >> i) & 1u) << (b->nbits % 8));
        b->nbits++;
    }
}

static void bits_zeros( bits_t* b, unsigned count )
{
    while( count-- > 0 )
    {
        bits_put( b, 0, 1 );
    }
}

/** Huffman codes: the highest bit goes first */
static void bits_code( bits_t* b, uint32_t code, unsigned length )
{
    while( length-- > 0 )
    {
        bits_put( b, (code >> length) & 1u, 1 );
    }
}

static size_t bits_size( const bits_t* b )
{
    return (b->nbits + 7) / 8;
}

/** A symbol of the fixed literal/length code (RFC 1951, 3.2.6) */
static void bits_fixed_symbol( bits_t* b, unsigned symbol )
{
    if( symbol < 144 )      bits_code( b, 0x30 + symbol, 8 );
    else if( symbol < 256 ) bits_code( b, 0x190 + symbol - 144, 9 );
    else if( symbol < 280 ) bits_code( b, symbol - 256, 7 );
    else                    bits_code( b, 0xC0 + symbol - 280, 8 );
}

static void bits_fixed_header( bits_t* b, int final )
{
    bits_put( b, final ? 1 : 0, 1 );
    bits_put( b, 1, 2 );
}

/** Decodes into a buffer of the given size (a block of its own, to let AddressSanitizer watch) */
static int decode( const void* stream, size_t size, size_t capacity, uint8_t** out, size_t* out_size )
{
    uint8_t* in = exact_copy( stream, size );
    int result;

    *out = (uint8_t*)malloc( capacity ? capacity : 1 );
    result = dmcomp_inflate( in, size, *out, capacity, out_size );
    free( in );
    return result;
}

static int decode_bits( const bits_t* b, size_t capacity )
{
    uint8_t* out;
    size_t out_size;
    int result = decode( b->buf, bits_size( b ), capacity, &out, &out_size );
    free( out );
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
//      Tests
// ---------------------------------------------------------------------------------------------------------------------

static void test_zlib_vectors( void )
{
    size_t v;

    for( v = 0; v < VECTOR_COUNT; v++ )
    {
        const vector_t* vec = &g_vectors[v];
        uint8_t* out;
        size_t out_size = 0;

        CHECK_EQ( decode( vec->stream, vec->stream_size, vec->expected_size, &out, &out_size ), DMCOMP_OK );
        CHECK_EQ( out_size, vec->expected_size );
        CHECK( memcmp( out, vec->expected, vec->expected_size ) == 0 );
        free( out );
        if( g_failures ) printf( "  (vector '%s')\n", vec->name );
    }
}

static void test_trailing_data_is_ignored( void )
{
    uint8_t stream[sizeof(vec_dynamic) + 8];
    uint8_t out[sizeof(vec_text)];
    size_t out_size = 0;

    memcpy( stream, vec_dynamic, sizeof(vec_dynamic) );
    memset( stream + sizeof(vec_dynamic), 0xA5, 8 );
    CHECK_EQ( dmcomp_inflate( stream, sizeof(stream), out, sizeof(out), &out_size ), DMCOMP_OK );
    CHECK_EQ( out_size, sizeof(vec_text) - 1 );
    CHECK( memcmp( out, vec_text, out_size ) == 0 );
}

static void test_output_buffer_too_small( void )
{
    size_t v, capacity;

    for( v = 0; v < VECTOR_COUNT; v++ )
    {
        for( capacity = 0; capacity < g_vectors[v].expected_size; capacity += 1 + capacity / 4 )
        {
            uint8_t* out;
            size_t out_size = 0;

            CHECK_EQ( decode( g_vectors[v].stream, g_vectors[v].stream_size, capacity, &out, &out_size ),
                      DMCOMP_ERR_OVERFLOW );
            free( out );
        }
    }
}

static void test_every_prefix_is_truncated( void )
{
    size_t v, length;

    for( v = 0; v < VECTOR_COUNT; v++ )
    {
        for( length = 0; length < g_vectors[v].stream_size; length++ )
        {
            uint8_t* out;
            size_t out_size = 0;
            int result = decode( g_vectors[v].stream, length, g_vectors[v].expected_size + 16, &out, &out_size );

            // Cut short, the stream is either too short or - when the cut is inside a code - plainly wrong
            CHECK( result == DMCOMP_ERR_TRUNCATED || result == DMCOMP_ERR_DATA );
            if( result != DMCOMP_ERR_TRUNCATED && result != DMCOMP_ERR_DATA )
            {
                printf( "  (vector '%s', %zu of %zu bytes: %s)\n", g_vectors[v].name, length,
                        g_vectors[v].stream_size, dmcomp_strerror( result ) );
            }
            free( out );
        }
    }
}

static void test_parameters( void )
{
    uint8_t out[16];
    size_t out_size = 123;

    CHECK_EQ( dmcomp_inflate( vec_fixed_a, sizeof(vec_fixed_a), out, sizeof(out), NULL ), DMCOMP_ERR_PARAM );
    CHECK_EQ( dmcomp_inflate( NULL, 3, out, sizeof(out), &out_size ), DMCOMP_ERR_PARAM );
    CHECK_EQ( dmcomp_inflate( vec_fixed_a, sizeof(vec_fixed_a), NULL, 4, &out_size ), DMCOMP_ERR_PARAM );
    CHECK_EQ( dmcomp_inflate( NULL, 0, out, sizeof(out), &out_size ), DMCOMP_ERR_TRUNCATED );
    CHECK_EQ( out_size, 0 );
    CHECK( dmcomp_strerror( DMCOMP_OK ) != NULL && dmcomp_strerror( -1000 ) != NULL );
    CHECK( strcmp( dmcomp_strerror( DMCOMP_OK ), dmcomp_strerror( DMCOMP_ERR_DATA ) ) != 0 );
}

static void test_empty_output( void )
{
    // An empty fixed block (the only thing in the stream) and an empty stored block
    static const uint8_t fixed_empty[] = { 0x03, 0x00 };
    static const uint8_t stored_empty[] = { 0x01, 0x00, 0x00, 0xFF, 0xFF };
    uint8_t* out;
    size_t out_size = 99;

    CHECK_EQ( decode( fixed_empty, sizeof(fixed_empty), 0, &out, &out_size ), DMCOMP_OK );
    CHECK_EQ( out_size, 0 );
    free( out );
    CHECK_EQ( decode( stored_empty, sizeof(stored_empty), 0, &out, &out_size ), DMCOMP_OK );
    CHECK_EQ( out_size, 0 );
    free( out );
}

static void test_blocks_one_after_another( void )
{
    // A stored block "ab" (not the last), a fixed block "c" (not the last), a fixed block "abcabc" with a match (last)
    bits_t b;
    uint8_t* out;
    size_t out_size = 0;

    memset( &b, 0, sizeof(b) );
    bits_put( &b, 0, 1 ); bits_put( &b, 0, 2 );                     // Stored, not final
    bits_put( &b, 0, 5 );                                           // To the byte boundary (3 + 5 bits)
    bits_put( &b, 2, 16 ); bits_put( &b, 0xFFFD, 16 );
    bits_put( &b, 'a', 8 ); bits_put( &b, 'b', 8 );
    bits_fixed_header( &b, 0 );
    bits_fixed_symbol( &b, 'c' ); bits_fixed_symbol( &b, 256 );
    bits_fixed_header( &b, 1 );
    bits_fixed_symbol( &b, 257 );                                   // A match of 3...
    bits_code( &b, 2, 5 );                                          // ...3 bytes back (distance code 2 = 3)
    bits_fixed_symbol( &b, 256 );

    CHECK_EQ( decode( b.buf, bits_size( &b ), 6, &out, &out_size ), DMCOMP_OK );
    CHECK_EQ( out_size, 6 );
    CHECK( memcmp( out, "abcabc", 6 ) == 0 );
    free( out );
}

static void test_dynamic_without_distance_codes( void )
{
    // A dynamic block of two codes: literal 0 and the end of block, one bit each; no distance codes at all
    bits_t b;
    uint8_t* out;
    size_t out_size = 0;
    unsigned i;

    memset( &b, 0, sizeof(b) );
    bits_put( &b, 1, 1 ); bits_put( &b, 2, 2 );
    bits_put( &b, 0, 5 );                       // HLIT: 257 codes
    bits_put( &b, 0, 5 );                       // HDIST: 1 code
    bits_put( &b, 14, 4 );                      // HCLEN: 18 lengths of the code length codes
    {
        // Order 16 17 18 0 8 7 9 6 10 5 11 4 12 3 13 2 14 1: symbols 0 and 1 have a one bit code
        static const uint8_t order[18] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1 };
        for( i = 0; i < 18; i++ ) bits_put( &b, (order[i] == 0 || order[i] == 1) ? 1 : 0, 3 );
    }
    // The lengths: 1 for literal 0, 0 for 1..255, 1 for 256, 0 for the only distance code (symbol 0 is the bit 0)
    bits_code( &b, 1, 1 );
    for( i = 1; i < 256; i++ ) bits_code( &b, 0, 1 );
    bits_code( &b, 1, 1 );
    bits_code( &b, 0, 1 );
    // The data: literal 0 ('0' - the first code), end of block ('1')
    bits_code( &b, 0, 1 );
    bits_code( &b, 1, 1 );

    CHECK_EQ( decode( b.buf, bits_size( &b ), 4, &out, &out_size ), DMCOMP_OK );
    CHECK_EQ( out_size, 1 );
    CHECK_EQ( out[0], 0 );
    free( out );
}

static void test_invalid_streams( void )
{
    bits_t b;
    unsigned i;

    // Reserved block type
    memset( &b, 0, sizeof(b) );
    bits_put( &b, 1, 1 ); bits_put( &b, 3, 2 );
    CHECK_EQ( decode_bits( &b, 16 ), DMCOMP_ERR_DATA );

    // Stored: LEN and NLEN do not match
    memset( &b, 0, sizeof(b) );
    bits_put( &b, 1, 1 ); bits_put( &b, 0, 2 ); bits_put( &b, 0, 5 );
    bits_put( &b, 3, 16 ); bits_put( &b, 0x1234, 16 );
    bits_put( &b, 0, 24 );
    CHECK_EQ( decode_bits( &b, 16 ), DMCOMP_ERR_DATA );

    // Stored: the data is not all there
    memset( &b, 0, sizeof(b) );
    bits_put( &b, 1, 1 ); bits_put( &b, 0, 2 ); bits_put( &b, 0, 5 );
    bits_put( &b, 5, 16 ); bits_put( &b, 0xFFFA, 16 );
    bits_put( &b, 'a', 8 ); bits_put( &b, 'b', 8 );
    CHECK_EQ( decode_bits( &b, 16 ), DMCOMP_ERR_TRUNCATED );

    // Fixed: a match before the start of the output (distance 5 after one byte)
    memset( &b, 0, sizeof(b) );
    bits_fixed_header( &b, 1 );
    bits_fixed_symbol( &b, 'a' );
    bits_fixed_symbol( &b, 257 ); bits_code( &b, 4, 5 ); bits_put( &b, 0, 1 );
    bits_fixed_symbol( &b, 256 );
    CHECK_EQ( decode_bits( &b, 16 ), DMCOMP_ERR_DATA );

    // Fixed: the same distance is fine one byte later... (5 bytes of output first)
    memset( &b, 0, sizeof(b) );
    bits_fixed_header( &b, 1 );
    for( i = 0; i < 5; i++ ) bits_fixed_symbol( &b, 'a' + i );
    bits_fixed_symbol( &b, 257 ); bits_code( &b, 4, 5 ); bits_put( &b, 0, 1 );
    bits_fixed_symbol( &b, 256 );
    CHECK_EQ( decode_bits( &b, 16 ), DMCOMP_OK );

    // Fixed: length codes 286 and 287 are reserved
    for( i = 286; i <= 287; i++ )
    {
        memset( &b, 0, sizeof(b) );
        bits_fixed_header( &b, 1 );
        bits_fixed_symbol( &b, 'a' );
        bits_fixed_symbol( &b, i );
        bits_put( &b, 0, 16 );
        CHECK_EQ( decode_bits( &b, 16 ), DMCOMP_ERR_DATA );
    }

    // Fixed: distance codes 30 and 31 are reserved
    for( i = 30; i <= 31; i++ )
    {
        memset( &b, 0, sizeof(b) );
        bits_fixed_header( &b, 1 );
        bits_fixed_symbol( &b, 'a' );
        bits_fixed_symbol( &b, 257 ); bits_code( &b, i, 5 );
        bits_put( &b, 0, 16 );
        CHECK_EQ( decode_bits( &b, 16 ), DMCOMP_ERR_DATA );
    }

    // Fixed: the output is full before the end of the block
    memset( &b, 0, sizeof(b) );
    bits_fixed_header( &b, 1 );
    for( i = 0; i < 4; i++ ) bits_fixed_symbol( &b, 'a' );
    bits_fixed_symbol( &b, 256 );
    CHECK_EQ( decode_bits( &b, 3 ), DMCOMP_ERR_OVERFLOW );
    CHECK_EQ( decode_bits( &b, 4 ), DMCOMP_OK );

    // Dynamic: more literal/length codes than there are (HLIT 31 + 257 = 288)
    memset( &b, 0, sizeof(b) );
    bits_put( &b, 1, 1 ); bits_put( &b, 2, 2 );
    bits_put( &b, 31, 5 ); bits_put( &b, 0, 5 ); bits_put( &b, 0, 4 );
    bits_zeros( &b, 64 );
    CHECK_EQ( decode_bits( &b, 16 ), DMCOMP_ERR_DATA );

    // Dynamic: more distance codes than there are (HDIST 31 + 1 = 32)
    memset( &b, 0, sizeof(b) );
    bits_put( &b, 1, 1 ); bits_put( &b, 2, 2 );
    bits_put( &b, 0, 5 ); bits_put( &b, 31, 5 ); bits_put( &b, 0, 4 );
    bits_zeros( &b, 64 );
    CHECK_EQ( decode_bits( &b, 16 ), DMCOMP_ERR_DATA );

    // Dynamic: the code lengths of the code length alphabet over-subscribe the code (4 codes of one bit)
    memset( &b, 0, sizeof(b) );
    bits_put( &b, 1, 1 ); bits_put( &b, 2, 2 );
    bits_put( &b, 0, 5 ); bits_put( &b, 0, 5 ); bits_put( &b, 0, 4 );   // HCLEN 4: symbols 16, 17, 18, 0
    for( i = 0; i < 4; i++ ) bits_put( &b, 1, 3 );
    bits_zeros( &b, 64 );
    CHECK_EQ( decode_bits( &b, 16 ), DMCOMP_ERR_DATA );

    // Dynamic: the first length is "repeat the previous one" (symbols 16 and 0 have the codes '1' and '0')
    memset( &b, 0, sizeof(b) );
    bits_put( &b, 1, 1 ); bits_put( &b, 2, 2 );
    bits_put( &b, 0, 5 ); bits_put( &b, 0, 5 ); bits_put( &b, 0, 4 );
    bits_put( &b, 1, 3 ); bits_put( &b, 0, 3 ); bits_put( &b, 0, 3 ); bits_put( &b, 1, 3 );
    bits_code( &b, 1, 1 );                                              // Symbol 16...
    bits_zeros( &b, 64 );
    CHECK_EQ( decode_bits( &b, 16 ), DMCOMP_ERR_DATA );

    // Dynamic: no code for the end of block (literals 0 and 1 have codes, 256 does not)
    memset( &b, 0, sizeof(b) );
    bits_put( &b, 1, 1 ); bits_put( &b, 2, 2 );
    bits_put( &b, 0, 5 ); bits_put( &b, 0, 5 ); bits_put( &b, 14, 4 );
    {
        static const uint8_t order[18] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1 };
        for( i = 0; i < 18; i++ ) bits_put( &b, (order[i] == 0 || order[i] == 1) ? 1 : 0, 3 );
    }
    bits_code( &b, 1, 1 ); bits_code( &b, 1, 1 );                       // Lengths 1 and 1 for literals 0, 1
    for( i = 2; i < 258; i++ ) bits_code( &b, 0, 1 );                   // The rest (256, and the distance code): 0
    bits_put( &b, 0, 32 );
    CHECK_EQ( decode_bits( &b, 16 ), DMCOMP_ERR_DATA );

    // Nothing at all
    CHECK_EQ( dmcomp_inflate( "", 0, (uint8_t[1]){ 0 }, 1, &(size_t){ 0 } ), DMCOMP_ERR_TRUNCATED );
}

int main( void )
{
    RUN( test_zlib_vectors );
    RUN( test_trailing_data_is_ignored );
    RUN( test_output_buffer_too_small );
    RUN( test_every_prefix_is_truncated );
    RUN( test_parameters );
    RUN( test_empty_output );
    RUN( test_blocks_one_after_another );
    RUN( test_dynamic_without_distance_codes );
    RUN( test_invalid_streams );
    return finish( "test_vectors" );
}
