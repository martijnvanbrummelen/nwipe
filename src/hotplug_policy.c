/*
 *  hotplug_policy.c: Shared hotplug policy helpers for nwipe.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "hotplug_policy.h"

const char* nwipe_hotplug_basename( const char* path )
{
    const char* base;

    if( path == NULL )
    {
        return "";
    }

    base = strrchr( path, '/' );
    return base != NULL ? base + 1 : path;
}

int nwipe_hotplug_path_is_partition_at_root( const char* sysfs_root, const char* path )
{
    char sysfs_path[4096];
    int r;

    if( sysfs_root == NULL || path == NULL )
    {
        return 0;
    }

    r = snprintf( sysfs_path,
                  sizeof( sysfs_path ),
                  "%s/%s/partition",
                  sysfs_root,
                  nwipe_hotplug_basename( path ) );
    if( r < 0 || (size_t) r >= sizeof( sysfs_path ) )
    {
        return 0;
    }

    return access( sysfs_path, F_OK ) == 0;
}

int nwipe_hotplug_should_promote_record( nwipe_hotplug_record_kind_t kind, int active_jobs )
{
    return kind == NWIPE_HOTPLUG_RECORD_PENDING && active_jobs == 0;
}
