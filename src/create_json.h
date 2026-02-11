/*
 * create_json.h: Header file for JSON report generation routines
 *
 * Copyright Knogle <https://github.com/Knogle>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, version 2.
 */

#ifndef CREATE_JSON_H_
#define CREATE_JSON_H_

#include "context.h"

/**
 * Create the disk erase report in JSON format
 * @param c pointer to a drive context
 * @return returns 0 on success, -1 on failure
 */
int create_json( nwipe_context_t* c );

#endif
