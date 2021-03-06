/*
 * viking -- GPS Data and Topo Analyzer, Explorer, and Manager
 *
 * Copyright (C) 2003-2005, Evan Battaglia <gtoevan@gmx.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef _VIKING_CURL_DOWNLOAD_H
#define _VIKING_CURL_DOWNLOAD_H

#include <stdio.h>

#include "download.h"

void curl_download_init ();
int curl_download_get_url ( const char *hostname, const char *uri, FILE *f, DownloadOptions *options, gboolean ftp, time_t time_condition, void *handle );
int curl_download_uri ( const char *uri, FILE *f, DownloadOptions *options, time_t time_condition, void *handle );
void * curl_download_handle_init ();
void curl_download_handle_cleanup ( void * handle );

#endif
