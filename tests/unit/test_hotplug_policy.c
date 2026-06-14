#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "hotplug_policy.h"

static void test_basename_helper( void )
{
    assert( strcmp( nwipe_hotplug_basename( "/dev/sda" ), "sda" ) == 0 );
    assert( strcmp( nwipe_hotplug_basename( "nvme0n1" ), "nvme0n1" ) == 0 );
    assert( strcmp( nwipe_hotplug_basename( NULL ), "" ) == 0 );
}

static void test_partition_detection_with_root( void )
{
    char root_path[] = "/tmp/nwipe-hotplug-policy-XXXXXX";
    char devdir[4096];
    char partition_path[4096];
    FILE* fp;

    assert( mkdtemp( root_path ) != NULL );

    snprintf( devdir, sizeof( devdir ), "%s/sda", root_path );
    assert( mkdir( devdir, 0700 ) == 0 );
    snprintf( partition_path, sizeof( partition_path ), "%s/sda/partition", root_path );

    fp = fopen( partition_path, "w" );
    assert( fp != NULL );
    fclose( fp );

    assert( nwipe_hotplug_path_is_partition_at_root( root_path, "/dev/sda" ) == 1 );
    assert( nwipe_hotplug_path_is_partition_at_root( root_path, "/dev/sdb" ) == 0 );
    assert( nwipe_hotplug_path_is_partition_at_root( root_path, NULL ) == 0 );

    unlink( partition_path );
    rmdir( devdir );
    rmdir( root_path );
}

static void test_promote_helper( void )
{
    assert( nwipe_hotplug_should_promote_record( NWIPE_HOTPLUG_RECORD_PENDING, 0 ) == 1 );
    assert( nwipe_hotplug_should_promote_record( NWIPE_HOTPLUG_RECORD_PENDING, 1 ) == 0 );
    assert( nwipe_hotplug_should_promote_record( NWIPE_HOTPLUG_RECORD_ACTIVE, 0 ) == 0 );
    assert( nwipe_hotplug_should_promote_record( NWIPE_HOTPLUG_RECORD_DONE, 0 ) == 0 );
}

int main( void )
{
    test_basename_helper();
    test_partition_detection_with_root();
    test_promote_helper();
    return 0;
}
