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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <string.h>
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_UTIME_H
#include <utime.h>
#endif
#include <glib.h>
#include <glib/gstdio.h>
#include <glib/gi18n.h>


#include "download.h"

#include "curl_download.h"

static gboolean check_file_first_line(FILE* f, gchar *patterns[])
{
  gchar **s;
  gchar *bp;
  fpos_t pos;
  gchar buf[33];
  size_t nr;

  memset(buf, 0, sizeof(buf));
  fgetpos(f, &pos);
  rewind(f);
  nr = fread(buf, 1, sizeof(buf) - 1, f);
  fsetpos(f, &pos);
  for (bp = buf; (bp < (buf + sizeof(buf) - 1)) && (nr > (bp - buf)); bp++) {
    if (!(isspace(*bp)))
      break;
  }
  if ((bp >= (buf + sizeof(buf) -1)) || ((bp - buf) >= nr))
    return FALSE;
  for (s = patterns; *s; s++) {
    if (strncasecmp(*s, bp, strlen(*s)) == 0)
      return TRUE;
  }
  return FALSE;
}

gboolean a_check_html_file(FILE* f)
{
  gchar * html_str[] = {
    "<html",
    "<!DOCTYPE html",
    "<head",
    "<title",
    NULL
  };

  return check_file_first_line(f, html_str);
}

gboolean a_check_map_file(FILE* f)
{
  /* FIXME no more true since a_check_kml_file */
  return !a_check_html_file(f);
}

gboolean a_check_kml_file(FILE* f)
{
  gchar * kml_str[] = {
    "<?xml",
    NULL
  };

  return check_file_first_line(f, kml_str);
}

static GList *file_list = NULL;
static GMutex *file_list_mutex = NULL;

void a_download_init (void)
{
	file_list_mutex = g_mutex_new();
}

static gboolean lock_file(const char *fn)
{
	gboolean locked = FALSE;
	g_mutex_lock(file_list_mutex);
	if (g_list_find(file_list, fn) == NULL)
	{
		// The filename is not yet locked
		file_list = g_list_append(file_list, (gpointer)fn),
		locked = TRUE;
	}
	g_mutex_unlock(file_list_mutex);
	return locked;
}

static void unlock_file(const char *fn)
{
	g_mutex_lock(file_list_mutex);
	file_list = g_list_remove(file_list, (gconstpointer)fn);
	g_mutex_unlock(file_list_mutex);
}

static int download( const char *hostname, const char *uri, const char *fn, DownloadOptions *options, gboolean ftp, void *handle)
{
  FILE *f;
  int ret;
  gchar *tmpfilename;
  gboolean failure = FALSE;
  time_t time_condition = 0;

  /* Check file */
  if ( g_file_test ( fn, G_FILE_TEST_EXISTS ) == TRUE )
  {
    if (options != NULL && options->check_file_server_time) {
      /* Get the modified time of this file */
      struct stat buf;
      g_stat ( fn, &buf );
      time_condition = buf.st_mtime;
      if ( (time(NULL) - time_condition) < options->check_file_server_time )
				/* File cache is too recent, so return */
				return -3;
    } else {
      /* Nothing to do as file already exists, so return */
      return -3;
    }
  } else {
    gchar *dir = g_path_get_dirname ( fn );
    g_mkdir_with_parents ( dir , 0777 );
    g_free ( dir );
  }

  tmpfilename = g_strdup_printf("%s.tmp", fn);
  if (!lock_file ( tmpfilename ) )
  {
    g_debug("%s: Couldn't take lock on temporary file \"%s\"\n", __FUNCTION__, tmpfilename);
    g_free ( tmpfilename );
    return -4;
  }
  f = g_fopen ( tmpfilename, "w+b" );  /* truncate file and open it */
  if ( ! f ) {
    g_warning("Couldn't open temporary file \"%s\": %s", tmpfilename, g_strerror(errno));
    g_free ( tmpfilename );
    return -4;
  }

  /* Call the backend function */
  ret = curl_download_get_url ( hostname, uri, f, options, ftp, time_condition, handle );

  if (ret != DOWNLOAD_NO_ERROR && ret != DOWNLOAD_NO_NEWER_FILE) {
    g_debug("%s: download failed: curl_download_get_url=%d", __FUNCTION__, ret);
    failure = TRUE;
  }

  if (!failure && options != NULL && options->check_file != NULL && ! options->check_file(f)) {
    g_debug("%s: file content checking failed", __FUNCTION__);
    failure = TRUE;
  }

  if (failure)
  {
    g_warning(_("Download error: %s"), fn);
    g_remove ( tmpfilename );
    unlock_file ( tmpfilename );
    g_free ( tmpfilename );
    fclose ( f );
    f = NULL;
    g_remove ( fn ); /* couldn't create temporary. delete 0-byte file. */
    return -1;
  }

  if (ret == DOWNLOAD_NO_NEWER_FILE)  {
    g_remove ( tmpfilename );
#if GLIB_CHECK_VERSION(2,18,0)
    g_utime ( fn, NULL ); /* update mtime of local copy */
#else
    utimes ( fn, NULL ); /* update mtime of local copy */
#endif
  } else
    g_rename ( tmpfilename, fn ); /* move completely-downloaded file to permanent location */
  unlock_file ( tmpfilename );
  g_free ( tmpfilename );
  fclose ( f );
  f = NULL;
  return 0;
}

/* success = 0, -1 = couldn't connect, -2 HTTP error, -3 file exists, -4 couldn't write to file... */
/* uri: like "/uri.html?whatever" */
/* only reason for the "wrapper" is so we can do redirects. */
int a_http_download_get_url ( const char *hostname, const char *uri, const char *fn, DownloadOptions *opt, void *handle )
{
  return download ( hostname, uri, fn, opt, FALSE, handle );
}

int a_ftp_download_get_url ( const char *hostname, const char *uri, const char *fn, DownloadOptions *opt, void *handle )
{
  return download ( hostname, uri, fn, opt, TRUE, handle );
}

void * a_download_handle_init ()
{
  return curl_download_handle_init ();
}

void a_download_handle_cleanup ( void *handle )
{
  curl_download_handle_cleanup ( handle );
}
