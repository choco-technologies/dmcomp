/**
 * @file dmcomp_tool.c
 * @brief Command line tool: compress and decompress files with dmcomp (raw DEFLATE)
 *
 *   dmcomp_tool c [-l level] <input> <output>     compress
 *   dmcomp_tool d <input> <output> <size>         decompress (the size of the result is not in a raw stream)
 *   dmcomp_tool t [-l level] <input>...           print the compressed sizes (and check the round trip)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dmcomp.h"

static uint8_t* read_file( const char* path, size_t* size )
{
    FILE* f = fopen( path, "rb" );
    uint8_t* data;
    long length;

    if( f == NULL )
    {
        fprintf( stderr, "Cannot open %s\n", path );
        return NULL;
    }
    fseek( f, 0, SEEK_END );
    length = ftell( f );
    fseek( f, 0, SEEK_SET );
    data = (uint8_t*)malloc( length > 0 ? (size_t)length : 1 );
    if( data == NULL || fread( data, 1, (size_t)length, f ) != (size_t)length )
    {
        fprintf( stderr, "Cannot read %s\n", path );
        free( data );
        data = NULL;
    }
    fclose( f );
    *size = (size_t)length;
    return data;
}

static int write_file( const char* path, const void* data, size_t size )
{
    FILE* f = fopen( path, "wb" );
    int ok = f != NULL && fwrite( data, 1, size, f ) == size;
    if( f != NULL )
    {
        fclose( f );
    }
    if( !ok )
    {
        fprintf( stderr, "Cannot write %s\n", path );
    }
    return ok ? 0 : 1;
}

static int usage( void )
{
    fprintf( stderr,
             "Usage: dmcomp_tool c [-l level] <input> <output>\n"
             "       dmcomp_tool d <input> <output> <size of the result>\n"
             "       dmcomp_tool t [-l level] <input>...\n" );
    return 2;
}

int main( int argc, char** argv )
{
    int level = DMCOMP_LEVEL_DEFAULT;
    int arg = 2;

    if( argc < 3 )
    {
        return usage();
    }
    if( arg + 1 < argc && strcmp( argv[arg], "-l" ) == 0 )
    {
        level = atoi( argv[arg + 1] );
        arg += 2;
    }

    if( strcmp( argv[1], "c" ) == 0 && argc - arg == 2 )
    {
        size_t size, packed_size = 0;
        uint8_t* data = read_file( argv[arg], &size );
        uint8_t* packed;
        int result;

        if( data == NULL ) return 1;
        packed = (uint8_t*)malloc( dmcomp_deflate_bound( size ) );
        result = dmcomp_deflate( data, size, packed, dmcomp_deflate_bound( size ), &packed_size, level );
        if( result != DMCOMP_OK )
        {
            fprintf( stderr, "Compression failed: %s\n", dmcomp_strerror( result ) );
            return 1;
        }
        return write_file( argv[arg + 1], packed, packed_size );
    }
    if( strcmp( argv[1], "d" ) == 0 && argc - arg == 3 )
    {
        size_t size, out_capacity = (size_t)strtoul( argv[arg + 2], NULL, 10 ), out_size = 0;
        uint8_t* data = read_file( argv[arg], &size );
        uint8_t* out = (uint8_t*)malloc( out_capacity ? out_capacity : 1 );
        int result;

        if( data == NULL ) return 1;
        result = dmcomp_inflate( data, size, out, out_capacity, &out_size );
        if( result != DMCOMP_OK )
        {
            fprintf( stderr, "Decompression failed: %s\n", dmcomp_strerror( result ) );
            return 1;
        }
        return write_file( argv[arg + 1], out, out_size );
    }
    if( strcmp( argv[1], "t" ) == 0 && argc - arg >= 1 )
    {
        size_t total = 0, total_packed = 0;
        int failed = 0;

        for( ; arg < argc; arg++ )
        {
            size_t size, packed_size = 0, back_size = 0;
            uint8_t* data = read_file( argv[arg], &size );
            uint8_t* packed;
            uint8_t* back;
            int ok;

            if( data == NULL ) { failed++; continue; }
            packed = (uint8_t*)malloc( dmcomp_deflate_bound( size ) );
            back = (uint8_t*)malloc( size ? size : 1 );
            ok = dmcomp_deflate( data, size, packed, dmcomp_deflate_bound( size ), &packed_size, level ) == DMCOMP_OK &&
                 dmcomp_inflate( packed, packed_size, back, size, &back_size ) == DMCOMP_OK &&
                 back_size == size && (size == 0 || memcmp( back, data, size ) == 0);
            printf( "%-40s %8zu -> %8zu  %5.1f%%  %s\n", argv[arg], size, packed_size,
                    size ? 100.0 * (double)packed_size / (double)size : 0.0, ok ? "ok" : "ROUND TRIP FAILED" );
            failed += !ok;
            total += size;
            total_packed += packed_size;
            free( back ); free( packed ); free( data );
        }
        printf( "%-40s %8zu -> %8zu  %5.1f%%\n", "total", total, total_packed,
                total ? 100.0 * (double)total_packed / (double)total : 0.0 );
        return failed ? 1 : 0;
    }
    return usage();
}
