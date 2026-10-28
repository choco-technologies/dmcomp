/**
 * @file test_util.h
 * @brief A minimal test harness and the generators of test data
 */
#ifndef TEST_UTIL_H
#define TEST_UTIL_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_checks = 0;
static int g_failures = 0;

#define CHECK( cond ) \
    do { \
        g_checks++; \
        if( !(cond) ) { g_failures++; printf( "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond ); } \
    } while( 0 )

#define CHECK_EQ( actual, expected ) \
    do { \
        long long a_ = (long long)(actual), e_ = (long long)(expected); \
        g_checks++; \
        if( a_ != e_ ) { \
            g_failures++; \
            printf( "  FAIL %s:%d: %s == %s (got %lld, expected %lld)\n", __FILE__, __LINE__, #actual, #expected, a_, e_ ); \
        } \
    } while( 0 )

#define RUN( test ) \
    do { \
        int before_ = g_failures; \
        test(); \
        printf( "%-4s %s\n", g_failures == before_ ? "ok" : "FAIL", #test ); \
    } while( 0 )

static inline int finish( const char* name )
{
    printf( "%s: %d checks, %d failures\n", name, g_checks, g_failures );
    return g_failures == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------------------------------------------------
//      Deterministic random numbers (xorshift32) - the tests have to repeat
// ---------------------------------------------------------------------------------------------------------------------

static uint32_t g_rng = 0x2545F491u;

static inline void rng_seed( uint32_t seed )
{
    g_rng = seed ? seed : 1u;
}

static inline uint32_t rng_next( void )
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}

static inline uint32_t rng_below( uint32_t limit )
{
    return limit ? rng_next() % limit : 0;
}

// ---------------------------------------------------------------------------------------------------------------------
//      Test data
// ---------------------------------------------------------------------------------------------------------------------

typedef enum
{
    DATA_ZEROS,         //!< One long run
    DATA_RANDOM,        //!< Incompressible
    DATA_TEXT,          //!< Words from a small vocabulary
    DATA_RUNS,          //!< Runs of random lengths of random bytes
    DATA_PATTERN,       //!< A short pattern repeated
    DATA_CODE,          //!< 32 bit words with a few distinct values and small offsets (like machine code)
    DATA_SKEWED,        //!< Random bytes with a very uneven distribution (long Huffman codes)
    DATA_FAR_REPEAT,    //!< Random data repeated at a distance of 32767 bytes (the longest one the encoder uses)
    DATA_MIXED,         //!< Compressible and incompressible parts one after another
    DATA_KINDS
} data_kind_t;

static inline const char* data_name( data_kind_t kind )
{
    static const char* names[DATA_KINDS] =
    {
        "zeros", "random", "text", "runs", "pattern", "code", "skewed", "far-repeat", "mixed"
    };
    return names[kind];
}

static inline void fill_data( data_kind_t kind, uint8_t* buf, size_t size )
{
    static const char* words[] =
    {
        "module", "loader", "flash", "the", "of", "and", "driver", "device", "file", "system", "memory", "heap",
        "stack", "thread", "mount", "service", "config", "dmod", "boot", "bank", "update", "firmware", "compressed"
    };
    size_t i = 0;

    switch( kind )
    {
        case DATA_ZEROS:
            memset( buf, 0, size );
            break;
        case DATA_RANDOM:
            for( i = 0; i < size; i++ ) buf[i] = (uint8_t)rng_next();
            break;
        case DATA_TEXT:
            while( i < size )
            {
                const char* w = words[rng_below( sizeof(words) / sizeof(words[0]) )];
                while( *w && i < size ) buf[i++] = (uint8_t)*w++;
                if( i < size ) buf[i++] = rng_below( 12 ) == 0 ? '\n' : ' ';
            }
            break;
        case DATA_RUNS:
            while( i < size )
            {
                size_t run = 1 + rng_below( 300 );
                uint8_t value = (uint8_t)rng_next();
                while( run-- && i < size ) buf[i++] = value;
            }
            break;
        case DATA_PATTERN:
        {
            size_t period = 1 + rng_below( 40 );
            for( i = 0; i < size; i++ ) buf[i] = i < period ? (uint8_t)rng_next() : buf[i - period];
            break;
        }
        case DATA_CODE:
            for( i = 0; i + 4 <= size; i += 4 )
            {
                uint32_t w = 0xE0000000u | (rng_below( 6 ) << 24) | (rng_below( 8 ) << 16) | rng_below( 40 );
                buf[i] = (uint8_t)w; buf[i + 1] = (uint8_t)(w >> 8); buf[i + 2] = (uint8_t)(w >> 16); buf[i + 3] = (uint8_t)(w >> 24);
            }
            for( ; i < size; i++ ) buf[i] = 0;
            break;
        case DATA_SKEWED:
            // Probabilities fall by half with every symbol: the optimal code is as long as the alphabet is big
            for( i = 0; i < size; i++ )
            {
                uint32_t r = rng_next();
                unsigned symbol = 0;
                while( symbol < 40 && (r & 1u) == 0 ) { r >>= 1; symbol++; }
                buf[i] = (uint8_t)(symbol * 6);
            }
            break;
        case DATA_FAR_REPEAT:
            for( i = 0; i < size; i++ ) buf[i] = i < 32767 ? (uint8_t)rng_next() : buf[i - 32767];
            break;
        case DATA_MIXED:
            while( i < size )
            {
                size_t part = 1 + rng_below( 3000 );
                data_kind_t k = (data_kind_t)rng_below( DATA_MIXED );
                if( part > size - i ) part = size - i;
                fill_data( k, buf + i, part );
                i += part;
            }
            break;
        default:
            break;
    }
}

/** A copy of the data in a block of exactly that size: AddressSanitizer sees every access beyond it */
static inline uint8_t* exact_copy( const void* src, size_t size )
{
    uint8_t* copy = (uint8_t*)malloc( size ? size : 1 );
    if( copy && size )
    {
        memcpy( copy, src, size );
    }
    return copy;
}

#endif /* TEST_UTIL_H */
