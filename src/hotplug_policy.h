/*
 *  hotplug_policy.h: Shared hotplug policy helpers for nwipe.
 */

#ifndef HOTPLUG_POLICY_H_
#define HOTPLUG_POLICY_H_

#include <sys/types.h>

typedef enum
{
    NWIPE_HOTPLUG_RECORD_BASELINE = 0,
    NWIPE_HOTPLUG_RECORD_PENDING,
    NWIPE_HOTPLUG_RECORD_ACTIVE,
    NWIPE_HOTPLUG_RECORD_DONE,
    NWIPE_HOTPLUG_RECORD_BLOCKED
} nwipe_hotplug_record_kind_t;

const char* nwipe_hotplug_basename( const char* path );
int nwipe_hotplug_path_is_partition_at_root( const char* sysfs_root, const char* path );
int nwipe_hotplug_should_promote_record( nwipe_hotplug_record_kind_t kind, int active_jobs );

#endif /* HOTPLUG_POLICY_H_ */
