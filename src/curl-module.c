/* -*- mode: C; mode: fold; -*- */
/*
Copyright (C) 2005, 2006, 2007 John E. Davis

This file is part of the S-Lang Curl Module

The S-Lang Curl Module is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the
License, or (at your option) any later version.

The S-Lang Curl Module is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with this library; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307,
USA.  
*/

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <slang.h>

#include <curl/curl.h>
#include <curl/multi.h>

SLANG_MODULE(curl);
#include "version.h"

/* Unfortunately symbols such as CURLOPT_FTP_ACCOUNT are not macros and the 
 * only way to test for their existence is through the version number. Sigh.
 */
#define MAKE_CURL_VERSION(a,b,c) (((a)<<16) + ((b)<<8) + (c))
#define CURL_VERSION_GE(a,b,c) \
   (LIBCURL_VERSION_NUM >= MAKE_CURL_VERSION((a),(b),(c)))

static int Curl_Error = 0;
static SLtype Easy_Type_Id = 0;
static SLtype Multi_Type_Id = 0;

typedef struct Easy_Type
{
   CURL *handle;
   char *url;
   SLang_MMT_Type *mmt;		       /* parent MMT */
   unsigned int flags;
#define PERFORM_RUNNING	0x1

   char errbuf [CURL_ERROR_SIZE+1];

   SLang_Name_Type *write_callback;    /* int write(write_data, bytes) */
   SLang_Any_Type *write_data;

   SLang_Name_Type *read_callback;
   SLang_Any_Type *read_data;

   SLang_Name_Type *writeheader_callback;
   SLang_Any_Type *writeheader_data;

   SLang_Name_Type *progress_callback;
   SLang_Any_Type *progress_data;

   /* The data for the following fields must remain for the lifetime of this
    * struct.
    */
   /* See curl.h for why the 10000 and the mod are necessary */
#define NUM_OPT_STRINGS ((unsigned int)CURLOPT_LASTENTRY % 10000)
   char *opt_strings[NUM_OPT_STRINGS];
   struct curl_slist *httpheader;      /* For CURLOPT_HTTPHEADER */
   struct curl_slist *http200aliases;
   struct curl_slist *quote;
   struct curl_slist *postquote;
   struct curl_slist *prequote;
   struct curl_slist *source_quote;
   struct curl_slist *source_prequote;
   struct curl_slist *source_postquote;
   struct curl_httppost *httppost;

   struct Multi_Type *multi;	       /* NON-null if this is attached to a multi */
   struct Easy_Type *next;	       /* pointer to next one in multi stack */
}
Easy_Type;

typedef struct Multi_Type
{
   CURLM *mhandle;
   Easy_Type *ez;
   unsigned int flags;
   int length;
}
Multi_Type;

/*{{{ Easy_Type Functions */

static void free_easy_type (Easy_Type *ez)
{
   char **sp, **spmax;

   if (ez == NULL)
     return;

   if (ez->handle != NULL)
     curl_easy_cleanup (ez->handle);
   
   if (ez->url != NULL)
     SLang_free_slstring (ez->url);

   if (ez->write_callback != NULL) SLang_free_function (ez->write_callback);
   if (ez->write_data != NULL) SLang_free_anytype (ez->write_data);

   if (ez->read_callback != NULL) SLang_free_function (ez->read_callback);
   if (ez->read_data != NULL) SLang_free_anytype (ez->read_data);

   if (ez->writeheader_callback != NULL) SLang_free_function (ez->writeheader_callback);
   if (ez->writeheader_data != NULL) SLang_free_anytype (ez->writeheader_data);

   if (ez->progress_callback != NULL) SLang_free_function (ez->progress_callback);
   if (ez->progress_data != NULL) SLang_free_anytype (ez->progress_data);
   
   sp = ez->opt_strings;
   spmax = sp + NUM_OPT_STRINGS;
   while (sp < spmax)
     {
	if (*sp != NULL)
	  SLang_free_slstring (*sp);
	sp++;
     }
   if (ez->httpheader != NULL) curl_slist_free_all (ez->httpheader);
   if (ez->http200aliases != NULL) curl_slist_free_all (ez->http200aliases);
   if (ez->quote != NULL) curl_slist_free_all (ez->quote);
   if (ez->postquote != NULL) curl_slist_free_all (ez->postquote);
   if (ez->prequote != NULL) curl_slist_free_all (ez->prequote);
   if (ez->source_quote != NULL) curl_slist_free_all (ez->source_quote);
   if (ez->source_prequote != NULL) curl_slist_free_all (ez->source_prequote);
   if (ez->source_postquote != NULL) curl_slist_free_all (ez->source_postquote);

   SLfree ((char *) ez);	  
}

static void throw_curl_error (CURLcode err, char *buf)
{
   SLang_verror (Curl_Error, "%s: %s", curl_easy_strerror(err), buf);
}

static int Initialized = 0;
static void global_init (long *flagsp)
{
   long flags = *flagsp;

   /* This cannot be called more than once. */
   if (Initialized)
     return;

   flags &= CURL_GLOBAL_ALL;
       
   if (0 != curl_global_init (flags))
     {
	SLang_verror (SL_RunTime_Error, "curl_global_init failed");
	return;
     }
   Initialized = 1;
}

static void global_cleanup (void)
{
   curl_global_cleanup ();
   Initialized = 0;
}

static size_t write_function_internal (void *ptr, size_t size, size_t nmemb, 
				       SLang_Name_Type *write_callback,
				       SLang_Any_Type *write_data)
{
   SLang_BString_Type *bstr;
   int status;

   if (NULL == (bstr = SLbstring_create ((unsigned char *)ptr, size * nmemb)))
     {
	return (size_t)0;	       /* error */
     }
   if ((-1 == SLang_start_arg_list ())
       || (-1 == SLang_push_anytype (write_data))
       || (-1 == SLang_push_bstring (bstr))
       || (-1 == SLang_end_arg_list ())
       || (-1 == SLexecute_function (write_callback))
       || (-1 == SLang_pop_int (&status)))
     status = -1;

   SLbstring_free (bstr);

   if (status != -1)
     return size * nmemb;

   /* error */
   return (size_t)0;
}

/* slang: Int_Type write_function (writedata, string);
 * return values: success: 0, error: -1
 */
static size_t write_function (void *ptr, size_t size, size_t nmemb, void *stream)
{
   Easy_Type *ez;

   ez = (Easy_Type *) stream;
   return write_function_internal (ptr, size, nmemb, ez->write_callback, ez->write_data);
}

static size_t write_header_function (void *ptr, size_t size, size_t nmemb, void *stream)
{
   Easy_Type *ez;

   ez = (Easy_Type *) stream;
   return write_function_internal (ptr, size, nmemb, ez->writeheader_callback, ez->writeheader_data);
}

static int progress_function (void *clientp, double dltotal, double dlnow, double ultotal, double ulnow)
{
   Easy_Type *ez;
   int status;

   ez = (Easy_Type *)clientp;
   if ((-1 == SLang_start_arg_list ())
       || (-1 == SLang_push_anytype (ez->progress_data))
       || (-1 == SLang_push_double (dltotal))
       || (-1 == SLang_push_double (dlnow))
       || (-1 == SLang_push_double (ultotal))
       || (-1 == SLang_push_double (ulnow))
       || (-1 == SLang_end_arg_list ())
       || (-1 == SLexecute_function (ez->progress_callback))
       || (-1 == SLang_pop_int (&status)))
     status = -1;
   
   return status;
}

/* slang: Bstring read_function (Object, num_bytes)
 * 
 *   If NULL returned, operation will be aborted.
 */
static size_t read_function (void *ptr, size_t size, size_t nmemb, void *stream)
{
   Easy_Type *ez;
   SLang_BString_Type *bstr;
   unsigned int bytes_read, bytes_requested;
   unsigned char *bytes;

   bytes_requested = (unsigned int) (size * nmemb);

   ez = (Easy_Type *) stream;
   if ((-1 == SLang_start_arg_list ())
       || (-1 == ((ez->read_data == NULL)
		  ? SLang_push_mmt (ez->mmt)
		  : SLang_push_anytype (ez->read_data)))
       || (-1 == SLang_push_long (bytes_requested))
       || (-1 == SLang_end_arg_list ())
       || (-1 == SLexecute_function (ez->read_callback)))
     {
	return CURL_READFUNC_ABORT;
     }
   
   if (SLang_peek_at_stack () == SLANG_NULL_TYPE)
     {
	(void) SLang_pop_null ();
	return CURL_READFUNC_ABORT;
     }
   
   if (-1 == SLang_pop_bstring (&bstr))
     {
	return CURL_READFUNC_ABORT;
     }
   
   if (NULL == (bytes = SLbstring_get_pointer (bstr, &bytes_read)))
     {
	SLbstring_free (bstr);
	return CURL_READFUNC_ABORT;
     }

   if (bytes_read > bytes_requested)
     bytes_read = bytes_requested;
   
   memcpy ((char *) ptr, bytes, bytes_read);
   
   SLbstring_free (bstr);
   return bytes_read;
}

static int check_handle (Easy_Type *ez, unsigned int flags)
{
   if ((ez == NULL) || (ez->handle == NULL))
     {
	SLang_verror (SL_RunTime_Error, "Curl_Type object has already been closed and may not be reused");
	return -1;
     }
   if (ez->flags & flags)
     {
	SLang_verror (SL_RunTime_Error, "It is illegal to call this function while curl_perform is running");
	return -1;
     }

   return 0;
}

static SLang_MMT_Type *pop_easy_type (Easy_Type **ezp, unsigned int flags)
{
   SLang_MMT_Type *mmt;
   Easy_Type *ez;

   if (NULL == (mmt = SLang_pop_mmt (Easy_Type_Id)))
     {
	*ezp = NULL;
	return NULL;
     }
   
   ez = (Easy_Type *)SLang_object_from_mmt (mmt);
   if (-1 == check_handle (ez, flags))
     {
	SLang_free_mmt (mmt);
	return NULL;
     }
   *ezp = ez;
   return mmt;
}
   

static int set_long_opt (Easy_Type *ez, CURLoption opt, int nargs, int use_def, long val)
{
   CURLcode status;
   
   if ((nargs > 1)
       || ((nargs == 0) && (use_def == 0)))
     {
	SLang_verror (SL_INVALID_PARM, "Expecting a single value for this cURL option");
	return -1;
     }
   
   if (nargs && (-1 == SLang_pop_long (&val)))
     return -1;

   status = curl_easy_setopt (ez->handle, opt, val);
   if (status == CURLE_OK)
     return 0;

   throw_curl_error (status, ez->errbuf);
   return -1;
}

typedef size_t (*CFUNC_Type)(void *, size_t, size_t, void *);

static int set_function_opt (Easy_Type *ez, CURLoption opt, CURLoption data_opt, int nargs,
			     SLang_Name_Type **funcp, SLang_Any_Type **datap,
			     CFUNC_Type callback)
{
   SLang_Any_Type *data;
   SLang_Name_Type *func;

   if (nargs != 2)
     {
	SLang_verror (SL_INVALID_PARM, "Expecting two arguments for this option");
	return -1;
     }
   
   if (NULL == (func = SLang_pop_function ()))
     return -1;

   if (-1 == SLang_pop_anytype (&data))
     {
	SLang_free_function (func);
	return -1;
     }

   if ((CURLE_OK != curl_easy_setopt (ez->handle, opt, callback))
       || (CURLE_OK != curl_easy_setopt (ez->handle, data_opt, ez)))
     {
	SLang_free_function (func);
	SLang_free_anytype (data);
	return -1;
     }

   if (NULL != *funcp)
     SLang_free_function (*funcp);
   *funcp = func;
   
   if (NULL != *datap)
     SLang_free_anytype (*datap);
   *datap = data;
   
   return 0;
}

static int set_string_opt_internal (Easy_Type *ez, CURLoption opt, char *str)
{
   char *old;
   CURLcode status;
   int indx;

   indx = opt - (int)CURLOPTTYPE_OBJECTPOINT;
   if ((indx < 0) || (indx >= (int)NUM_OPT_STRINGS))
     {
	SLang_verror (SL_Internal_Error, "Unexpected Curl option value %d", opt);
	return -1;
     }
   old = ez->opt_strings[indx];

   if (old == str)
     return 0;

   if (NULL == (str = SLang_create_slstring (str)))
     return -1;
   status = curl_easy_setopt (ez->handle, opt, str);
   if (status != CURLE_OK)
     {
	throw_curl_error (status, ez->errbuf);
	SLang_free_slstring (str);
	return -1;
     }
   ez->opt_strings[indx] = str;
   SLang_free_slstring (old);
   return 0;
}

static int set_string_opt (Easy_Type *ez, CURLoption opt, int nargs)
{
   char *str;
   int ret;

   if (nargs != 1)
     {
	SLang_verror (SL_INVALID_PARM, "Expecting a single string argument");
	return -1;
     }
   if (-1 == SLang_pop_slstring (&str))
     return -1;

   ret = set_string_opt_internal (ez, opt, str);
   SLang_free_slstring (str);
   return ret;
}

static int set_strlist_opt (Easy_Type *ez, CURLoption opt, int nargs, 
			    struct curl_slist **slistp)
{
   struct curl_slist *slist = NULL;
   CURLcode status;

   if (nargs > 1)
     {
	SLang_verror (SL_INVALID_PARM, "This option requires an array of strings");
	return -1;
     }
   
   if (nargs == 1)
     {
	SLang_Array_Type *at;
	char **sp, **spmax;

	if (-1 == SLang_pop_array_of_type (&at, SLANG_STRING_TYPE))
	  return -1;
	sp = (char **)at->data;
	spmax = sp + at->num_elements;
   
	while (sp < spmax)
	  {
	     if (*sp != NULL)
	       {
		  struct curl_slist *olist = slist;
		  if (NULL == (slist = curl_slist_append (olist, *sp)))
		    {
		       SLang_verror (Curl_Error, "Error in building a cURL list");
		       curl_slist_free_all (olist);
		       SLang_free_array (at);
		       return -1;
		    }
	       }
	     sp++;
	  }
	SLang_free_array (at);
     }
   
   if (*slistp != NULL)
     {
	curl_slist_free_all (*slistp);
	*slistp = NULL;
     }

   status = curl_easy_setopt (ez->handle, opt, slist);

   if (status != CURLE_OK)
     {
	if (slist != NULL)
	  curl_slist_free_all (slist);
	throw_curl_error (status, ez->errbuf);
	return -1;
     }
   *slistp = slist;
   return 0;
}


static int do_setopt (Easy_Type *ez, CURLoption opt, int nargs)
{
   switch (opt)
     {
	/* behavior options (long arg) */
      case CURLOPT_VERBOSE:
      case CURLOPT_HEADER:
      case CURLOPT_NOPROGRESS:
      case CURLOPT_NOSIGNAL:	       /* May not want to support this */
	return set_long_opt (ez, opt, nargs, 1, 1L);

	/* Callback Options */
      case CURLOPT_WRITEFUNCTION:
	return set_function_opt (ez, opt, CURLOPT_WRITEDATA, nargs, &ez->write_callback, &ez->write_data, write_function);

      case CURLOPT_READFUNCTION:
	return set_function_opt (ez, opt, CURLOPT_READDATA, nargs, &ez->read_callback, &ez->read_data, read_function);

      case CURLOPT_IOCTLFUNCTION:
	break;

      case CURLOPT_PROGRESSFUNCTION:
	(void) curl_easy_setopt (ez->handle, CURLOPT_NOPROGRESS, 0);
	return set_function_opt (ez, opt, CURLOPT_PROGRESSDATA, nargs, &ez->progress_callback, &ez->progress_data, (CFUNC_Type)progress_function);
	break;

      case CURLOPT_HEADERFUNCTION:
	return set_function_opt (ez, opt, CURLOPT_WRITEHEADER, nargs, &ez->writeheader_callback, &ez->writeheader_data, write_header_function);

      case CURLOPT_DEBUGFUNCTION:
      case CURLOPT_SSL_CTX_FUNCTION:
	break;
	
	/* data options */
      case CURLOPT_WRITEDATA:
	/* return set_write_data_opt (ez, opt, CURLOPT_WRITEDATA, nargs); */
      case CURLOPT_READDATA:
      case CURLOPT_IOCTLDATA:
      case CURLOPT_PROGRESSDATA:
      case CURLOPT_WRITEHEADER:
      case CURLOPT_DEBUGDATA:
      case CURLOPT_SSL_CTX_DATA:
	break;

	/* error options */
      case CURLOPT_ERRORBUFFER:
      case CURLOPT_STDERR:
      case CURLOPT_FAILONERROR:
	break;

	/* network options */
      case CURLOPT_URL:		       /* required option */
      case CURLOPT_PROXY:
	return set_string_opt (ez, opt, nargs);

      case CURLOPT_PROXYPORT:
	return set_long_opt (ez, opt, nargs, 0, 0);

      case CURLOPT_PROXYTYPE:
	return set_long_opt (ez, opt, nargs, 0, 0);   /* only certain values allowed */

      case CURLOPT_HTTPPROXYTUNNEL:
	return set_long_opt (ez, opt, nargs, 1, 1L);

      case CURLOPT_INTERFACE:
	return set_string_opt (ez, opt, nargs);

      case CURLOPT_DNS_CACHE_TIMEOUT:
	return set_long_opt (ez, opt, nargs, 1, 0L);

      case CURLOPT_DNS_USE_GLOBAL_CACHE:   /* obsolete and not encouraged */
	break;

      case CURLOPT_BUFFERSIZE:
	break;

      case CURLOPT_PORT:
	return set_long_opt (ez, opt, nargs, 0, 0L);

      case CURLOPT_TCP_NODELAY:
	return set_long_opt (ez, opt, nargs, 1, 1L);

	/* names and password options */
      case CURLOPT_NETRC:
	return set_long_opt (ez, opt, nargs, 0, 0L);   /* FIXME */

      case CURLOPT_NETRC_FILE:
      case CURLOPT_USERPWD:
      case CURLOPT_PROXYUSERPWD:
	return set_string_opt (ez, opt, nargs);

      case CURLOPT_HTTPAUTH:
      case CURLOPT_PROXYAUTH:
	return set_long_opt (ez, opt, nargs, 0, 0L);   /* FIXME */

	/* HTTP options */
      case CURLOPT_AUTOREFERER:
	return set_long_opt (ez, opt, nargs, 1, 1L);
      case CURLOPT_ENCODING:
	return set_string_opt (ez, opt, nargs);

      case CURLOPT_FOLLOWLOCATION:
      case CURLOPT_UNRESTRICTED_AUTH:
	return set_long_opt (ez, opt, nargs, 1, 1L);
      case CURLOPT_MAXREDIRS:
	return set_long_opt (ez, opt, nargs, 0, 0L);

      case CURLOPT_PUT:		       /* deprecated */
      case CURLOPT_POST:
	return set_long_opt (ez, opt, nargs, 1, 1L);

      case CURLOPT_POSTFIELDS:	       /* FIXME allow binary */
	return set_string_opt (ez, opt, nargs);
      case CURLOPT_POSTFIELDSIZE:      /* FIXME: combine with above */
	break;
      case CURLOPT_POSTFIELDSIZE_LARGE:/* FIXME: combine with above */
	break;

      case CURLOPT_HTTPPOST:	       /* FIXME: linked list */
	break;

      case CURLOPT_REFERER:
	return set_string_opt (ez, opt, nargs);

      case CURLOPT_USERAGENT:
	return set_string_opt (ez, opt, nargs);

      case CURLOPT_HTTPHEADER:
	return set_strlist_opt (ez, opt, nargs, &ez->httpheader);
	break;
      case CURLOPT_HTTP200ALIASES:
	return set_strlist_opt (ez, opt, nargs, &ez->http200aliases);
	break;

      case CURLOPT_COOKIE:	       /* FIXME: check format */
	return set_string_opt (ez, opt, nargs);

      case CURLOPT_COOKIEFILE:
      case CURLOPT_COOKIEJAR:
	return set_string_opt (ez, opt, nargs);

      case CURLOPT_COOKIESESSION:
      case CURLOPT_HTTPGET:
	return set_long_opt (ez, opt, nargs, 1, 1L);

      case CURLOPT_HTTP_VERSION:       /* FIXME: Check values */
	return set_long_opt (ez, opt, nargs, 0, 0L);

	/* FTP options */
      case CURLOPT_FTPPORT:
	return set_string_opt (ez, opt, nargs);
      case CURLOPT_QUOTE:	       /* FIXME: linked list */
	return set_strlist_opt (ez, opt, nargs, &ez->quote);
      case CURLOPT_POSTQUOTE:
	return set_strlist_opt (ez, opt, nargs, &ez->postquote);
      case CURLOPT_PREQUOTE:
	return set_strlist_opt (ez, opt, nargs, &ez->prequote);

      case CURLOPT_FTPLISTONLY:
      case CURLOPT_FTPAPPEND:
      case CURLOPT_FTP_USE_EPRT:
      case CURLOPT_FTP_USE_EPSV:
      case CURLOPT_FTP_CREATE_MISSING_DIRS:
      case CURLOPT_FTP_RESPONSE_TIMEOUT:
	return set_long_opt (ez, opt, nargs, 1, 1L);

      case CURLOPT_FTP_SSL:
	return set_long_opt (ez, opt, nargs, 0, 0L);   /* FIXME: specific values */

#if CURL_VERSION_GE(7,14,0)
      case CURLOPT_SOURCE_URL:
	return set_string_opt (ez, opt, nargs);
#endif
      case CURLOPT_SOURCE_USERPWD:
	return set_string_opt (ez, opt, nargs);
#if CURL_VERSION_GE(7,14,0)
      case CURLOPT_SOURCE_QUOTE:
	return set_strlist_opt (ez, opt, nargs, &ez->source_quote);
#endif
      case CURLOPT_SOURCE_PREQUOTE:
	return set_strlist_opt (ez, opt, nargs, &ez->source_prequote);
      case CURLOPT_SOURCE_POSTQUOTE:
	return set_strlist_opt (ez, opt, nargs, &ez->source_postquote);
#if CURL_VERSION_GE(7,14,0)
      case CURLOPT_FTP_ACCOUNT:
	return set_string_opt (ez, opt, nargs);
#endif
	/* protocol options */
      case CURLOPT_TRANSFERTEXT:
      case CURLOPT_CRLF:
	return set_long_opt (ez, opt, nargs, 1, 1L);

      case CURLOPT_RANGE:	       /* FIXME: NULL allowed */
	return set_string_opt (ez, opt, nargs);

      case CURLOPT_RESUME_FROM:
	return set_long_opt (ez, opt, nargs, 0, 0L);

      case CURLOPT_RESUME_FROM_LARGE:  /* FIXME: curl_off_t */
	break;

      case CURLOPT_CUSTOMREQUEST:
	return set_string_opt (ez, opt, nargs);

      case CURLOPT_FILETIME:
      case CURLOPT_NOBODY:
      case CURLOPT_INFILESIZE:
	return set_long_opt (ez, opt, nargs, 1, 1L);

      case CURLOPT_INFILESIZE_LARGE:   /* FIXME: curl_off_t */
	break;

      case CURLOPT_UPLOAD:
	return set_long_opt (ez, opt, nargs, 1, 1L);
      case CURLOPT_MAXFILESIZE:
	return set_long_opt (ez, opt, nargs, 0, 0L);
      case CURLOPT_MAXFILESIZE_LARGE:
	break;			       /* FIXME: curl_off_t */

      case CURLOPT_TIMECONDITION:      /* FIXME: specific values */
	return set_long_opt (ez, opt, nargs, 0, 0L);

      case CURLOPT_TIMEVALUE:
	return set_long_opt (ez, opt, nargs, 0, 0L);

	/* Connection options */
      case CURLOPT_TIMEOUT:
      case CURLOPT_LOW_SPEED_LIMIT:
      case CURLOPT_LOW_SPEED_TIME:
      case CURLOPT_MAXCONNECTS:
	return set_long_opt (ez, opt, nargs, 0, 0L);

      case CURLOPT_CLOSEPOLICY:	       /* FIXME: specific values */
	return set_long_opt (ez, opt, nargs, 0, 0L);

      case CURLOPT_FORBID_REUSE:       /* may want to avoid this */
#if 0
	return set_long_opt (ez, opt, nargs, 0, 0L);
#else
	break;
#endif

      case CURLOPT_CONNECTTIMEOUT:
	return set_long_opt (ez, opt, nargs, 0, 0L);
	
      case CURLOPT_IPRESOLVE:	       /* FIXME: specific values */
	return set_long_opt (ez, opt, nargs, 0, 0L);

	/* SSL and Security Options */
      case CURLOPT_SSLCERT:
      case CURLOPT_SSLCERTTYPE:
      /* case CURLOPT_SSLCERTPASSWD: */
      case CURLOPT_SSLKEY:
      case CURLOPT_SSLKEYTYPE:
      case CURLOPT_SSLKEYPASSWD:
      case CURLOPT_SSLENGINE:
      case CURLOPT_SSLENGINE_DEFAULT:
	return set_string_opt (ez, opt, nargs);

      case CURLOPT_SSL_VERIFYPEER:
	return set_long_opt (ez, opt, nargs, 1, 1L);

      case CURLOPT_SSLVERSION:	       /* FIXME: specific values */
	return set_long_opt (ez, opt, nargs, 0, 0L);

      case CURLOPT_CAINFO:
      case CURLOPT_CAPATH:
      case CURLOPT_RANDOM_FILE:
      case CURLOPT_EGDSOCKET:
	return set_string_opt (ez, opt, nargs);

      case CURLOPT_SSL_VERIFYHOST:
	return set_long_opt (ez, opt, nargs, 0, 0L);

      case CURLOPT_SSL_CIPHER_LIST:    /* FIXME: check syntax */
	return set_string_opt (ez, opt, nargs);

      case CURLOPT_KRB4LEVEL:
	return set_string_opt (ez, opt, nargs);
	
	/* Misc */
      case CURLOPT_PRIVATE:	       /* FIXME: provide interface?? */
	break;

      case CURLOPT_SHARE:
	break;

	/* Telnet Options */
      case CURLOPT_TELNETOPTIONS:      /* FIXME: linked list */
	break;
	
      default:
	break;
     }
   SLang_verror (SL_INVALID_PARM, "cURL option is unknown or unsupported");
   return -1;
}

	
static void setopt_intrin (void)
{
   SLang_MMT_Type *mmt;
   Easy_Type *ez;
   int opt;
   int nargs = SLang_Num_Function_Args - 2;

   if (nargs < 0)
     {
	SLang_verror (SL_USAGE_ERROR, "Usage: curl_setopt(curlobj, option, value)");
	return;
     }
   
   if (-1 == SLreverse_stack (nargs + 2))
     return;

   if (NULL == (mmt = pop_easy_type (&ez, PERFORM_RUNNING)))
     return;

   if (-1 == SLang_pop_int (&opt))
     {
	SLang_free_mmt (mmt);
	return;
     }
   
   (void) do_setopt (ez, (CURLoption)opt, nargs);

   SLang_free_mmt (mmt);
}

static void new_curl_intrin (char *url)
{
   SLang_MMT_Type *mmt;
   Easy_Type *ez;
   CURLcode status;

   if (NULL == (ez = (Easy_Type *) SLcalloc (1, sizeof (Easy_Type))))
     return;
   
   if (NULL == (ez->handle = curl_easy_init ()))
     {
	SLang_verror (SL_RunTime_Error, "curl_easy_init failed");
	free_easy_type (ez);
	return;
     }

   if (NULL == (ez->url = SLang_create_slstring (url)))
     {
	free_easy_type (ez);
	return;
     }

   if (CURLE_OK != (status = curl_easy_setopt (ez->handle, CURLOPT_ERRORBUFFER, ez->errbuf)))
     {
	SLang_verror (SL_RunTime_Error, "curl_easy_setopt: %s", curl_easy_strerror (status));
	free_easy_type (ez);
	return;
     }

   if (NULL == (mmt = SLang_create_mmt (Easy_Type_Id, (VOID_STAR) ez)))
     {
	free_easy_type (ez);
	return;
     }
   ez->mmt = mmt;

   if (-1 == set_string_opt_internal (ez, CURLOPT_URL, url))
     {
	SLang_free_mmt (mmt);
	return;
     }

   if ((CURLE_OK != (status = curl_easy_setopt (ez->handle, CURLOPT_VERBOSE, 0L)))
       || (CURLE_OK != (status = curl_easy_setopt (ez->handle, CURLOPT_NOPROGRESS, 1L)))
       || (CURLE_OK != (status = curl_easy_setopt (ez->handle, CURLOPT_PRIVATE, (char *)ez))))
     {
	SLang_verror (Curl_Error, "curl_easy_setopt: %s", curl_easy_strerror (status));
	SLang_free_mmt (mmt);
	return;
     }

   if (-1 == SLang_push_mmt (mmt))
     SLang_free_mmt (mmt);
}

static void perform_intrin (void)
{
   SLang_MMT_Type *mmt;
   Easy_Type *ez;
   CURLcode status;

   if (NULL == (mmt = pop_easy_type (&ez, PERFORM_RUNNING)))
     return;

   ez->flags |= PERFORM_RUNNING;
   if (CURLE_OK != (status = curl_easy_perform (ez->handle)))
     throw_curl_error (status, ez->errbuf);
   ez->flags &= ~PERFORM_RUNNING;

   SLang_free_mmt (mmt);
}

static void close_intrin (void)
{
   SLang_MMT_Type *mmt;
   Easy_Type *ez;

   if (NULL == (mmt = pop_easy_type (&ez, PERFORM_RUNNING)))
     return;

   if (ez->multi != NULL)
     {
	SLang_verror (SL_INVALID_PARM, "The object must first be removed from the Curl_Multi_Type before it can be closed");
	SLang_free_mmt (mmt);
	return;
     }

   curl_easy_cleanup (ez->handle);
   ez->handle = NULL;

   SLang_free_mmt (mmt);
}

   
static void get_url_intrin (void)
{
   SLang_MMT_Type *mmt;
   Easy_Type *ez;

   if (NULL == (mmt = pop_easy_type (&ez, 0)))
     return;
   
   (void) SLang_push_string (ez->url);
   SLang_free_mmt (mmt);
}

static int push_slist (struct curl_slist *slist)
{
   SLindex_Type num;
   struct curl_slist *s;
   SLang_Array_Type *at;
   char **data;

   if (slist == NULL)
     return SLang_push_null ();
   
   num = 0;
   s = slist;
   while (s != NULL)
     {
	num++;
	s = s->next;
     }
   
   at = SLang_create_array (SLANG_STRING_TYPE, 0, NULL, &num, 1);
   if (at == NULL)
     return -1;
   
   data = (char **) at->data;
   s = slist;
   while (s != NULL)
     {
	if ((slist->data != NULL)
	    && (NULL == (*data = SLang_create_slstring (slist->data))))
	  {
	     SLang_free_array (at);
	     return -1;
	  }
	data++;
	s = s->next;
     }
   return SLang_push_array (at, 1);
}

static void get_info_intrin (void)
{
   SLang_MMT_Type *mmt;
   Easy_Type *ez;
   CURLcode status;
   char *str;
   long lvar;
   double dvar;
   struct curl_slist *slist;
   int info;

   if (-1 == SLang_pop_int (&info))
     return;

   if (NULL == (mmt = pop_easy_type (&ez, 0)))
     return;

   switch ((CURLINFO) info)
     {
      case CURLINFO_EFFECTIVE_URL:
      case CURLINFO_CONTENT_TYPE:
	status = curl_easy_getinfo (ez->handle, info, &str);
	if (status == CURLE_OK)
	  (void) SLang_push_string (str);
	break;
	
      case CURLINFO_RESPONSE_CODE:
      case CURLINFO_HTTP_CONNECTCODE:
      case CURLINFO_FILETIME:
      case CURLINFO_REDIRECT_COUNT:
      case CURLINFO_HEADER_SIZE:
      case CURLINFO_REQUEST_SIZE:
      case CURLINFO_SSL_VERIFYRESULT:
      case CURLINFO_HTTPAUTH_AVAIL:
      case CURLINFO_PROXYAUTH_AVAIL:
      case CURLINFO_OS_ERRNO:
      case CURLINFO_NUM_CONNECTS:
	status = curl_easy_getinfo (ez->handle, info, &lvar);
	if (status == CURLE_OK)
	  (void) SLang_push_long (lvar);
	break;

      case CURLINFO_TOTAL_TIME:
      case CURLINFO_NAMELOOKUP_TIME:
      case CURLINFO_CONNECT_TIME:
      case CURLINFO_PRETRANSFER_TIME:
      case CURLINFO_STARTTRANSFER_TIME:
      case CURLINFO_REDIRECT_TIME:
      case CURLINFO_SIZE_UPLOAD:
      case CURLINFO_SIZE_DOWNLOAD:
      case CURLINFO_SPEED_DOWNLOAD:
      case CURLINFO_SPEED_UPLOAD:
      case CURLINFO_CONTENT_LENGTH_DOWNLOAD:
      case CURLINFO_CONTENT_LENGTH_UPLOAD:
	status = curl_easy_getinfo (ez->handle, info, &dvar);
	if (status == CURLE_OK)
	  (void) SLang_push_double (dvar);
	break;

      case CURLINFO_SSL_ENGINES:
      /* case CURLINFO_COOKIELIST: */
	status = curl_easy_getinfo (ez->handle, info, &slist);
	if (status == CURLE_OK)
	  {
	     (void) push_slist (slist);
	     curl_slist_free_all (slist);
	  }
	break;

      case CURLINFO_PRIVATE:
      default:
	SLang_verror (SL_INVALID_PARM, "Unknown of unsupported info type");
	status = 0;
	break;
     }
   
   
   if (status != CURLE_OK)
     throw_curl_error (status, ez->errbuf);

   SLang_free_mmt (mmt);
}

/*}}}*/

/*{{{ Multi_Type Functions */

static void throw_multi_error (CURLMcode err)
{
   SLang_verror (Curl_Error, "%s", curl_multi_strerror(err));
}

static SLang_MMT_Type *pop_multi_type (Multi_Type **mp, unsigned int flags)
{
   SLang_MMT_Type *mmt;
   Multi_Type *m;
   Easy_Type *ez;

   *mp = NULL;
   if (NULL == (mmt = SLang_pop_mmt (Multi_Type_Id)))
     return NULL;

   m = (Multi_Type *)SLang_object_from_mmt (mmt);
   if (m->mhandle == NULL)
     {
	SLang_verror (SL_INVALID_PARM, "The Curl_Multi_Type object has already been closed");
	SLang_free_mmt (mmt);
	return NULL;
     }

   if (m->flags & flags)
     {
	SLang_free_mmt (mmt);
	SLang_verror (SL_INVALID_PARM, "The Curl_Multi_Type is in an invalid state for this operation");
	return NULL;
     }
   
   ez = m->ez;
   while (ez != NULL)
     {
	if (-1 == check_handle (ez, flags))
	  {
	     SLang_free_mmt (mmt);
	     return NULL;
	  }
	ez = ez->next;
     }
   *mp = m;
   return mmt;
}

static int multi_remove_handle_internal (Multi_Type *m, Easy_Type *ez)
{
   CURLMcode status;

   status = curl_multi_remove_handle (m->mhandle, ez->handle);
   ez->multi = NULL;
   ez->next = NULL;
   SLang_free_mmt (ez->mmt);		       /* free from multi */
   m->length -= 1;

   if (status != CURLM_OK)
     {
	throw_multi_error (status);
	return -1;
     }
   return 0;
}

static void multi_close_internal (Multi_Type *m)
{
   Easy_Type *ez;
   
   ez = m->ez;
   while (ez != NULL)
     {
	Easy_Type *next = ez->next;
	(void) multi_remove_handle_internal (m, ez);
	ez = next;
     }
   m->ez = NULL;
   if (m->mhandle != NULL)
     (void) curl_multi_cleanup (m->mhandle);
   m->mhandle = NULL;
}

static void free_multi_type (Multi_Type *m)
{
   if (m == NULL)
     return;

   multi_close_internal (m);
   SLfree ((char *) m);
}


static void multi_remove_handle (void)
{
   Easy_Type *ez, *prev;
   SLang_MMT_Type *ez_mmt, *m_mmt;
   Multi_Type *m;
   
   if (NULL == (ez_mmt = pop_easy_type (&ez, PERFORM_RUNNING)))
     return;
   if (NULL == (m_mmt = pop_multi_type (&m, PERFORM_RUNNING)))
     {
	SLang_free_mmt (ez_mmt);
	return;
     }
   prev = m->ez;
   if (prev == ez)
     m->ez = ez->next;
   else
     {
	while (1)
	  {
	     if (prev->next == NULL)
	       goto free_return;
	     if (prev->next == ez)
	       break;
	     prev = prev->next;
	  }
	prev->next = ez->next;
     }

   (void) multi_remove_handle_internal (m, ez);
   
   /* drop */
   
   free_return:
   SLang_free_mmt (ez_mmt);
   SLang_free_mmt (m_mmt);
}

static void multi_close (void)
{
   SLang_MMT_Type *mmt;
   Multi_Type *m;
   
   if (NULL == (mmt = pop_multi_type (&m, PERFORM_RUNNING)))
     return;

   multi_close_internal (m);
   SLang_free_mmt (mmt);
}

static void multi_add_handle (void)
{
   Easy_Type *ez;
   SLang_MMT_Type *ez_mmt, *m_mmt;
   Multi_Type *m;
   CURLMcode status;
   
   if (NULL == (ez_mmt = pop_easy_type (&ez, PERFORM_RUNNING)))
     return;
   if (NULL == (m_mmt = pop_multi_type (&m, PERFORM_RUNNING)))
     {
	SLang_free_mmt (ez_mmt);
	return;
     }
   if (ez->multi != NULL)
     {
	SLang_verror (SL_INVALID_PARM, "Curl_Type is already attached to a Curl_Multi_Type object");
	SLang_free_mmt (ez_mmt);
	SLang_free_mmt (m_mmt);
	return;
     }
   
   status = curl_multi_add_handle (m->mhandle, ez->handle);
   if (status != CURLM_OK)
     {
	throw_multi_error (status);
	SLang_free_mmt (ez_mmt);
	SLang_free_mmt (m_mmt);
	return;
     }
   
   ez->multi = m;
   ez->next = m->ez;
   m->ez = ez;
   m->length += 1;

   /* The ez_mmt is not freed because it is now reference my the multi_type */
   SLang_free_mmt (m_mmt);
}

static int do_select_on_multi (Multi_Type *m, double dt)
{
   struct timeval tv;
   fd_set read_fds, write_fds, execpt_fds;
   int max_fd;
   CURLMcode status;
   int ret;

   /* Avoid a ridiculous wait period, 30 days ought to be enough */
   if (dt > 30*86400)
     dt = 30*86400;

   tv.tv_sec = (unsigned long) dt;
   tv.tv_usec = (unsigned long) ((dt - tv.tv_sec) * 1e6);

   FD_ZERO(&read_fds);
   FD_ZERO(&write_fds);
   FD_ZERO(&execpt_fds);

   status  = curl_multi_fdset (m->mhandle, &read_fds, &write_fds, &execpt_fds, &max_fd);
   if (status != CURLM_OK)
     {
	throw_multi_error (status);
	return -1;
     }
   
   ret = select (max_fd + 1, &read_fds, &write_fds, &execpt_fds, &tv);
   if (ret == -1)
     {
	/* Pretend like something is available to read/write  */
	ret = 1;
     }

   return ret;
}

static int multi_perform_intrin (void)
{
   SLang_MMT_Type *mmt;
   Multi_Type *m;
   Easy_Type *ez;
   CURLMcode status;
   int running_handles;
   double dt = 0.0;

   if (SLang_Num_Function_Args == 2)
     {
	if (-1 == SLang_pop_double (&dt))
	  return -1;
	if (dt < 0.0)
	  dt = 0.0;
     }

   if (NULL == (mmt = pop_multi_type (&m, PERFORM_RUNNING)))
     return -1;
   
   ez = m->ez;
   if (ez == NULL)
     {
	SLang_verror (SL_INVALID_PARM, "The Curl_Multi_Type object has no handles");
	SLang_free_mmt (mmt);
	return -1;
     }

   while (ez != NULL)
     {
	ez->flags |= PERFORM_RUNNING;
	ez = ez->next;
     }

   running_handles = 0;
   if (dt > 0.0)
     {
	int ret = do_select_on_multi (m, dt);
	if (ret == -1)
	  running_handles = -1;
     }

   if (running_handles == 0) while (1)
     {
	status = curl_multi_perform (m->mhandle, &running_handles);
	if (status == CURLM_CALL_MULTI_PERFORM)
	  {
	     if (0 != SLang_handle_interrupt ())
	       break;
	     
	     continue;
	  }
	
	if (status == CURLM_OK)
	  break;
	
	throw_multi_error (status);
	break;
     }
   
   ez = m->ez;
   while (ez != NULL)
     {
	ez->flags &= ~PERFORM_RUNNING;
	ez = ez->next;
     }

   SLang_free_mmt (mmt);
   return running_handles;
}

static void multi_info_read (void)
{
   SLang_MMT_Type *mmt;
   Multi_Type *m;
   CURLMsg *msg;
   int msgs_in_queue;
   SLang_Ref_Type *ref = NULL;

   if (SLang_Num_Function_Args == 2)
     {
	if (-1 == SLang_pop_ref (&ref))
	  return;
     }

   if (NULL == (mmt = pop_multi_type (&m, PERFORM_RUNNING)))
     {
	SLang_free_ref (ref);
	return;
     }

   while (NULL != (msg = curl_multi_info_read (m->mhandle, &msgs_in_queue)))
     {
	CURLcode status;
	Easy_Type *ez;

	if (msg->msg != CURLMSG_DONE)
	  continue;
	
	status = curl_easy_getinfo (msg->easy_handle, CURLINFO_PRIVATE, &ez);
	if ((status != CURLE_OK) || (ez == NULL))
	  {
	     throw_curl_error (status, "Internal cURL error");
	     break;
	  }
	
	if (ref != NULL)
	  {
	     int i = (int) msg->data.result;
	     if (-1 == SLang_assign_to_ref (ref, SLANG_INT_TYPE, (VOID_STAR)&i))
	       break;
	  }
	(void) SLang_push_mmt (ez->mmt);
	goto free_return;
     }
   
   (void) SLang_push_null ();
   
   free_return:

   if (ref != NULL)
     SLang_free_ref (ref);
   SLang_free_mmt (mmt);
}

   
static void new_multi_intrin (void)
{
   SLang_MMT_Type *mmt;
   Multi_Type *m;

   if (NULL == (m = (Multi_Type *) SLcalloc (1, sizeof (Multi_Type))))
     return;
   
   if (NULL == (m->mhandle = curl_multi_init ()))
     {
	SLang_verror (Curl_Error, "curl_multi_init failed");
	free_multi_type (m);
	return;
     }

   if (NULL == (mmt = SLang_create_mmt (Multi_Type_Id, (VOID_STAR) m)))
     {
	free_multi_type (m);
	return;
     }

   if (-1 == SLang_push_mmt (mmt))
     SLang_free_mmt (mmt);
}

static int get_multi_length_intrin (void)
{
   SLang_MMT_Type *mmt;
   Multi_Type *m;
   int len;

   if (NULL == (mmt = pop_multi_type (&m, 0)))
     return -1;
   
   len = m->length;
   SLang_free_mmt (mmt);
   return len;
}

/*}}}*/


static void escape_intrin (SLang_BString_Type *bstr)
{
   SLang_MMT_Type *mmt;
   Easy_Type *ez;
   char *escaped_string;
   char *url;
   unsigned int len;

   if (NULL == (url = (char *) SLbstring_get_pointer (bstr, &len)))
     return;

   if (NULL == (mmt = pop_easy_type (&ez, 0))) 
     return;

#if CURL_VERSION_GE(7,15,4)
   escaped_string = curl_easy_escape (ez->handle, url, (int)len);
#else
   escaped_string = curl_escape (url, (int) len);
#endif

   if (escaped_string == NULL)
     SLang_set_error (Curl_Error);
   else
     {
	char *str = SLang_create_slstring (escaped_string);
	if (str != NULL)
	  {
	     (void) SLang_push_string (str);
	     SLang_free_slstring (str);
	  }
	curl_free (escaped_string);
     }
   SLang_free_mmt(mmt);
}

static void unescape_intrin (char *url)
{
   SLang_MMT_Type *mmt;
   Easy_Type *ez;
   char *unescaped_string;
#if CURL_VERSION_GE(7,15,4)
   int outlength;
#endif

   if (NULL == (mmt = pop_easy_type (&ez, 0))) 
     return;

#if CURL_VERSION_GE(7,15,4)
   unescaped_string = curl_easy_unescape (ez->handle, url, 0, &outlength);
#else
   unescaped_string = curl_unescape (url, 0);
#endif

   if (unescaped_string == NULL)
     SLang_set_error (Curl_Error);
   else
     {
	int len = strlen (unescaped_string);
#if CURL_VERSION_GE(7,15,4)
	if (len != outlength)
	  {
	     SLang_BString_Type *bstr = SLbstring_create ((unsigned char *)unescaped_string, outlength);
	     if (bstr != NULL)
	       {
		  (void) SLang_push_bstring (bstr);
		  SLbstring_free (bstr);
	       }
	  }
	else
#endif
	  {
	     char *str = SLang_create_nslstring (unescaped_string, len);
	     if (str != NULL)
	       {
		  (void) SLang_push_string (str);
		  SLang_free_slstring (str);
	       }
	  }
	curl_free (unescaped_string);
     }
   SLang_free_mmt(mmt);
}

static char *easy_strerror_intrin (int *codep)
{
   return (char *) curl_easy_strerror ((CURLcode) *codep);
}


static SLang_Intrin_Fun_Type Module_Intrinsics [] =
{
   MAKE_INTRINSIC_1("curl_new", new_curl_intrin, SLANG_VOID_TYPE, SLANG_STRING_TYPE),
   MAKE_INTRINSIC_0("curl_setopt", setopt_intrin, SLANG_VOID_TYPE),
   MAKE_INTRINSIC_1("curl_global_init", global_init, SLANG_VOID_TYPE, SLANG_LONG_TYPE),
   MAKE_INTRINSIC_0("curl_global_cleanup", global_cleanup, SLANG_VOID_TYPE),
   MAKE_INTRINSIC_0("curl_perform", perform_intrin, SLANG_VOID_TYPE),
   MAKE_INTRINSIC_0("curl_close", close_intrin, SLANG_VOID_TYPE),
   MAKE_INTRINSIC_0("curl_get_info", get_info_intrin, SLANG_VOID_TYPE),

   MAKE_INTRINSIC_0("curl_multi_new", new_multi_intrin, SLANG_VOID_TYPE),
   MAKE_INTRINSIC_0("curl_multi_perform", multi_perform_intrin, SLANG_INT_TYPE),
   MAKE_INTRINSIC_0("curl_multi_remove_handle", multi_remove_handle, SLANG_VOID_TYPE),
   MAKE_INTRINSIC_0("curl_multi_add_handle", multi_add_handle, SLANG_VOID_TYPE),
   MAKE_INTRINSIC_0("curl_multi_close", multi_close, SLANG_VOID_TYPE),
   MAKE_INTRINSIC_0("curl_multi_info_read", multi_info_read, SLANG_VOID_TYPE),
   
   MAKE_INTRINSIC_I("curl_easy_strerror", easy_strerror_intrin, SLANG_STRING_TYPE),
   MAKE_INTRINSIC_I("curl_strerror", easy_strerror_intrin, SLANG_STRING_TYPE),

   MAKE_INTRINSIC_1("curl_easy_escape", escape_intrin, SLANG_VOID_TYPE, SLANG_BSTRING_TYPE),
   MAKE_INTRINSIC_1("curl_easy_unescape", unescape_intrin, SLANG_VOID_TYPE, SLANG_STRING_TYPE),

   /* Local Additions */
   MAKE_INTRINSIC_0("curl_get_url", get_url_intrin, SLANG_VOID_TYPE),
   MAKE_INTRINSIC_0("curl_multi_length", get_multi_length_intrin, SLANG_INT_TYPE),
   
   SLANG_END_INTRIN_FUN_TABLE
};

static SLang_Intrin_Var_Type Module_Variables [] =
{
   MAKE_VARIABLE("_curl_module_version_string", &Module_Version_String, SLANG_STRING_TYPE, 1),
   SLANG_END_INTRIN_VAR_TABLE
};

static SLang_IConstant_Type Module_IConstants [] =
{
   MAKE_ICONSTANT("_curl_module_version", MODULE_VERSION_NUMBER),
   MAKE_ICONSTANT("CURLOPT_VERBOSE", CURLOPT_VERBOSE),
   MAKE_ICONSTANT("CURLOPT_HEADER", CURLOPT_HEADER),
   MAKE_ICONSTANT("CURLOPT_NOPROGRESS", CURLOPT_NOPROGRESS),
   MAKE_ICONSTANT("CURLOPT_NOSIGNAL", CURLOPT_NOSIGNAL),
   MAKE_ICONSTANT("CURLOPT_WRITEFUNCTION", CURLOPT_WRITEFUNCTION),
   MAKE_ICONSTANT("CURLOPT_READFUNCTION", CURLOPT_READFUNCTION),
   MAKE_ICONSTANT("CURLOPT_IOCTLFUNCTION", CURLOPT_IOCTLFUNCTION),
   MAKE_ICONSTANT("CURLOPT_PROGRESSFUNCTION", CURLOPT_PROGRESSFUNCTION),
   MAKE_ICONSTANT("CURLOPT_HEADERFUNCTION", CURLOPT_HEADERFUNCTION),
   MAKE_ICONSTANT("CURLOPT_DEBUGFUNCTION", CURLOPT_DEBUGFUNCTION),
   MAKE_ICONSTANT("CURLOPT_SSL_CTX_FUNCTION", CURLOPT_SSL_CTX_FUNCTION),
   MAKE_ICONSTANT("CURLOPT_WRITEDATA", CURLOPT_WRITEDATA),
   MAKE_ICONSTANT("CURLOPT_READDATA", CURLOPT_READDATA),
   MAKE_ICONSTANT("CURLOPT_IOCTLDATA", CURLOPT_IOCTLDATA),
   MAKE_ICONSTANT("CURLOPT_PROGRESSDATA", CURLOPT_PROGRESSDATA),
   MAKE_ICONSTANT("CURLOPT_WRITEHEADER", CURLOPT_WRITEHEADER),
   MAKE_ICONSTANT("CURLOPT_DEBUGDATA", CURLOPT_DEBUGDATA),
   MAKE_ICONSTANT("CURLOPT_SSL_CTX_DATA", CURLOPT_SSL_CTX_DATA),
   MAKE_ICONSTANT("CURLOPT_ERRORBUFFER", CURLOPT_ERRORBUFFER),
   MAKE_ICONSTANT("CURLOPT_STDERR", CURLOPT_STDERR),
   MAKE_ICONSTANT("CURLOPT_FAILONERROR", CURLOPT_FAILONERROR),
   MAKE_ICONSTANT("CURLOPT_URL", CURLOPT_URL),
   MAKE_ICONSTANT("CURLOPT_PROXY", CURLOPT_PROXY),
   MAKE_ICONSTANT("CURLOPT_PROXYPORT", CURLOPT_PROXYPORT),
   MAKE_ICONSTANT("CURLOPT_PROXYTYPE", CURLOPT_PROXYTYPE),
   MAKE_ICONSTANT("CURLOPT_HTTPPROXYTUNNEL", CURLOPT_HTTPPROXYTUNNEL),
   MAKE_ICONSTANT("CURLOPT_INTERFACE", CURLOPT_INTERFACE),
   MAKE_ICONSTANT("CURLOPT_DNS_CACHE_TIMEOUT", CURLOPT_DNS_CACHE_TIMEOUT),
   MAKE_ICONSTANT("CURLOPT_DNS_USE_GLOBAL_CACHE", CURLOPT_DNS_USE_GLOBAL_CACHE),
   MAKE_ICONSTANT("CURLOPT_BUFFERSIZE", CURLOPT_BUFFERSIZE),
   MAKE_ICONSTANT("CURLOPT_PORT", CURLOPT_PORT),
   MAKE_ICONSTANT("CURLOPT_TCP_NODELAY", CURLOPT_TCP_NODELAY),
   MAKE_ICONSTANT("CURLOPT_NETRC", CURLOPT_NETRC),
   MAKE_ICONSTANT("CURLOPT_NETRC_FILE", CURLOPT_NETRC_FILE),
   MAKE_ICONSTANT("CURLOPT_USERPWD", CURLOPT_USERPWD),
   MAKE_ICONSTANT("CURLOPT_PROXYUSERPWD", CURLOPT_PROXYUSERPWD),
   MAKE_ICONSTANT("CURLOPT_HTTPAUTH", CURLOPT_HTTPAUTH),
   MAKE_ICONSTANT("CURLOPT_PROXYAUTH", CURLOPT_PROXYAUTH),
   MAKE_ICONSTANT("CURLOPT_AUTOREFERER", CURLOPT_AUTOREFERER),
   MAKE_ICONSTANT("CURLOPT_ENCODING", CURLOPT_ENCODING),
   MAKE_ICONSTANT("CURLOPT_FOLLOWLOCATION", CURLOPT_FOLLOWLOCATION),
   MAKE_ICONSTANT("CURLOPT_UNRESTRICTED_AUTH", CURLOPT_UNRESTRICTED_AUTH),
   MAKE_ICONSTANT("CURLOPT_MAXREDIRS", CURLOPT_MAXREDIRS),
   MAKE_ICONSTANT("CURLOPT_PUT", CURLOPT_PUT),
   MAKE_ICONSTANT("CURLOPT_POST", CURLOPT_POST),
   MAKE_ICONSTANT("CURLOPT_POSTFIELDS", CURLOPT_POSTFIELDS),
   MAKE_ICONSTANT("CURLOPT_POSTFIELDSIZE", CURLOPT_POSTFIELDSIZE),
   MAKE_ICONSTANT("CURLOPT_POSTFIELDSIZE_LARGE", CURLOPT_POSTFIELDSIZE_LARGE),
   MAKE_ICONSTANT("CURLOPT_HTTPPOST", CURLOPT_HTTPPOST),
   MAKE_ICONSTANT("CURLOPT_REFERER", CURLOPT_REFERER),
   MAKE_ICONSTANT("CURLOPT_USERAGENT", CURLOPT_USERAGENT),
   MAKE_ICONSTANT("CURLOPT_HTTPHEADER", CURLOPT_HTTPHEADER),
   MAKE_ICONSTANT("CURLOPT_HTTP200ALIASES", CURLOPT_HTTP200ALIASES),
   MAKE_ICONSTANT("CURLOPT_COOKIE", CURLOPT_COOKIE),
   MAKE_ICONSTANT("CURLOPT_COOKIEFILE", CURLOPT_COOKIEFILE),
   MAKE_ICONSTANT("CURLOPT_COOKIEJAR", CURLOPT_COOKIEJAR),
   MAKE_ICONSTANT("CURLOPT_COOKIESESSION", CURLOPT_COOKIESESSION),
   MAKE_ICONSTANT("CURLOPT_HTTPGET", CURLOPT_HTTPGET),
   MAKE_ICONSTANT("CURLOPT_HTTP_VERSION", CURLOPT_HTTP_VERSION),
   MAKE_ICONSTANT("CURLOPT_FTPPORT", CURLOPT_FTPPORT),
   MAKE_ICONSTANT("CURLOPT_QUOTE", CURLOPT_QUOTE),
   MAKE_ICONSTANT("CURLOPT_POSTQUOTE", CURLOPT_POSTQUOTE),
   MAKE_ICONSTANT("CURLOPT_PREQUOTE", CURLOPT_PREQUOTE),
   MAKE_ICONSTANT("CURLOPT_FTPLISTONLY", CURLOPT_FTPLISTONLY),
   MAKE_ICONSTANT("CURLOPT_FTPAPPEND", CURLOPT_FTPAPPEND),
   MAKE_ICONSTANT("CURLOPT_FTP_USE_EPRT", CURLOPT_FTP_USE_EPRT),
   MAKE_ICONSTANT("CURLOPT_FTP_USE_EPSV", CURLOPT_FTP_USE_EPSV),
   MAKE_ICONSTANT("CURLOPT_FTP_CREATE_MISSING_DIRS", CURLOPT_FTP_CREATE_MISSING_DIRS),
   MAKE_ICONSTANT("CURLOPT_FTP_RESPONSE_TIMEOUT", CURLOPT_FTP_RESPONSE_TIMEOUT),
   MAKE_ICONSTANT("CURLOPT_FTP_SSL", CURLOPT_FTP_SSL),
#if CURL_VERSION_GE(7,14,0)
   MAKE_ICONSTANT("CURLOPT_SOURCE_URL", CURLOPT_SOURCE_URL),
#endif
   MAKE_ICONSTANT("CURLOPT_SOURCE_USERPWD", CURLOPT_SOURCE_USERPWD),
#if CURL_VERSION_GE(7,14,0)
   MAKE_ICONSTANT("CURLOPT_SOURCE_QUOTE", CURLOPT_SOURCE_QUOTE),
#endif
   MAKE_ICONSTANT("CURLOPT_SOURCE_PREQUOTE", CURLOPT_SOURCE_PREQUOTE),
   MAKE_ICONSTANT("CURLOPT_SOURCE_POSTQUOTE", CURLOPT_SOURCE_POSTQUOTE),
#if CURL_VERSION_GE(7,14,0)
   MAKE_ICONSTANT("CURLOPT_FTP_ACCOUNT", CURLOPT_FTP_ACCOUNT),
#endif
   MAKE_ICONSTANT("CURLOPT_TRANSFERTEXT", CURLOPT_TRANSFERTEXT),
   MAKE_ICONSTANT("CURLOPT_CRLF", CURLOPT_CRLF),
   MAKE_ICONSTANT("CURLOPT_RANGE", CURLOPT_RANGE),
   MAKE_ICONSTANT("CURLOPT_RESUME_FROM", CURLOPT_RESUME_FROM),
   MAKE_ICONSTANT("CURLOPT_RESUME_FROM_LARGE", CURLOPT_RESUME_FROM_LARGE),
   MAKE_ICONSTANT("CURLOPT_CUSTOMREQUEST", CURLOPT_CUSTOMREQUEST),
   MAKE_ICONSTANT("CURLOPT_FILETIME", CURLOPT_FILETIME),
   MAKE_ICONSTANT("CURLOPT_NOBODY", CURLOPT_NOBODY),
   MAKE_ICONSTANT("CURLOPT_INFILESIZE", CURLOPT_INFILESIZE),
   MAKE_ICONSTANT("CURLOPT_INFILESIZE_LARGE", CURLOPT_INFILESIZE_LARGE),
   MAKE_ICONSTANT("CURLOPT_UPLOAD", CURLOPT_UPLOAD),
   MAKE_ICONSTANT("CURLOPT_MAXFILESIZE", CURLOPT_MAXFILESIZE),
   MAKE_ICONSTANT("CURLOPT_MAXFILESIZE_LARGE", CURLOPT_MAXFILESIZE_LARGE),
   MAKE_ICONSTANT("CURLOPT_TIMECONDITION", CURLOPT_TIMECONDITION),
   MAKE_ICONSTANT("CURLOPT_TIMEVALUE", CURLOPT_TIMEVALUE),
   MAKE_ICONSTANT("CURLOPT_TIMEOUT", CURLOPT_TIMEOUT),
   MAKE_ICONSTANT("CURLOPT_LOW_SPEED_LIMIT", CURLOPT_LOW_SPEED_LIMIT),
   MAKE_ICONSTANT("CURLOPT_LOW_SPEED_TIME", CURLOPT_LOW_SPEED_TIME),
   MAKE_ICONSTANT("CURLOPT_MAXCONNECTS", CURLOPT_MAXCONNECTS),
   MAKE_ICONSTANT("CURLOPT_CLOSEPOLICY", CURLOPT_CLOSEPOLICY),
   MAKE_ICONSTANT("CURLOPT_FORBID_REUSE", CURLOPT_FORBID_REUSE),
   MAKE_ICONSTANT("CURLOPT_CONNECTTIMEOUT", CURLOPT_CONNECTTIMEOUT),
   MAKE_ICONSTANT("CURLOPT_IPRESOLVE", CURLOPT_IPRESOLVE),
   MAKE_ICONSTANT("CURLOPT_SSLCERT", CURLOPT_SSLCERT),
   MAKE_ICONSTANT("CURLOPT_SSLCERTTYPE", CURLOPT_SSLCERTTYPE),
   MAKE_ICONSTANT("CURLOPT_SSLCERTPASSWD", CURLOPT_SSLCERTPASSWD),
   MAKE_ICONSTANT("CURLOPT_SSLKEY", CURLOPT_SSLKEY),
   MAKE_ICONSTANT("CURLOPT_SSLKEYTYPE", CURLOPT_SSLKEYTYPE),
   MAKE_ICONSTANT("CURLOPT_SSLKEYPASSWD", CURLOPT_SSLKEYPASSWD),
   MAKE_ICONSTANT("CURLOPT_SSLENGINE", CURLOPT_SSLENGINE),
   MAKE_ICONSTANT("CURLOPT_SSLENGINE_DEFAULT", CURLOPT_SSLENGINE_DEFAULT),
   MAKE_ICONSTANT("CURLOPT_SSL_VERIFYPEER", CURLOPT_SSL_VERIFYPEER),
   MAKE_ICONSTANT("CURLOPT_SSLVERSION", CURLOPT_SSLVERSION),
   MAKE_ICONSTANT("CURLOPT_CAINFO", CURLOPT_CAINFO),
   MAKE_ICONSTANT("CURLOPT_CAPATH", CURLOPT_CAPATH),
   MAKE_ICONSTANT("CURLOPT_RANDOM_FILE", CURLOPT_RANDOM_FILE),
   MAKE_ICONSTANT("CURLOPT_EGDSOCKET", CURLOPT_EGDSOCKET),
   MAKE_ICONSTANT("CURLOPT_SSL_VERIFYHOST", CURLOPT_SSL_VERIFYHOST),
   MAKE_ICONSTANT("CURLOPT_SSL_CIPHER_LIST", CURLOPT_SSL_CIPHER_LIST),
   MAKE_ICONSTANT("CURLOPT_KRB4LEVEL", CURLOPT_KRB4LEVEL),
   MAKE_ICONSTANT("CURLOPT_PRIVATE", CURLOPT_PRIVATE),
   MAKE_ICONSTANT("CURLOPT_SHARE", CURLOPT_SHARE),
   MAKE_ICONSTANT("CURLOPT_TELNETOPTIONS", CURLOPT_TELNETOPTIONS),
   
   MAKE_ICONSTANT("CURL_GLOBAL_ALL", CURL_GLOBAL_ALL),
   MAKE_ICONSTANT("CURL_GLOBAL_SSL", CURL_GLOBAL_SSL),
   MAKE_ICONSTANT("CURL_GLOBAL_WIN32", CURL_GLOBAL_WIN32),
   MAKE_ICONSTANT("CURL_GLOBAL_NOTHING", CURL_GLOBAL_NOTHING),

   MAKE_ICONSTANT("CURL_NETRC_IGNORED", CURL_NETRC_IGNORED),
   MAKE_ICONSTANT("CURL_NETRC_OPTIONAL", CURL_NETRC_OPTIONAL),
   MAKE_ICONSTANT("CURL_NETRC_REQUIRED", CURL_NETRC_REQUIRED),

   MAKE_ICONSTANT("CURLINFO_EFFECTIVE_URL", CURLINFO_EFFECTIVE_URL),
   MAKE_ICONSTANT("CURLINFO_RESPONSE_CODE", CURLINFO_RESPONSE_CODE),
   MAKE_ICONSTANT("CURLINFO_TOTAL_TIME", CURLINFO_TOTAL_TIME),
   MAKE_ICONSTANT("CURLINFO_NAMELOOKUP_TIME", CURLINFO_NAMELOOKUP_TIME),
   MAKE_ICONSTANT("CURLINFO_CONNECT_TIME", CURLINFO_CONNECT_TIME),
   MAKE_ICONSTANT("CURLINFO_PRETRANSFER_TIME", CURLINFO_PRETRANSFER_TIME),
   MAKE_ICONSTANT("CURLINFO_SIZE_UPLOAD", CURLINFO_SIZE_UPLOAD),
   MAKE_ICONSTANT("CURLINFO_SIZE_DOWNLOAD", CURLINFO_SIZE_DOWNLOAD),
   MAKE_ICONSTANT("CURLINFO_SPEED_DOWNLOAD", CURLINFO_SPEED_DOWNLOAD),
   MAKE_ICONSTANT("CURLINFO_SPEED_UPLOAD", CURLINFO_SPEED_UPLOAD),
   MAKE_ICONSTANT("CURLINFO_HEADER_SIZE", CURLINFO_HEADER_SIZE),
   MAKE_ICONSTANT("CURLINFO_REQUEST_SIZE", CURLINFO_REQUEST_SIZE),
   MAKE_ICONSTANT("CURLINFO_SSL_VERIFYRESULT", CURLINFO_SSL_VERIFYRESULT),
   MAKE_ICONSTANT("CURLINFO_FILETIME", CURLINFO_FILETIME),
   MAKE_ICONSTANT("CURLINFO_CONTENT_LENGTH_DOWNLOAD", CURLINFO_CONTENT_LENGTH_DOWNLOAD),
   MAKE_ICONSTANT("CURLINFO_CONTENT_LENGTH_UPLOAD", CURLINFO_CONTENT_LENGTH_UPLOAD),
   MAKE_ICONSTANT("CURLINFO_STARTTRANSFER_TIME", CURLINFO_STARTTRANSFER_TIME),
   MAKE_ICONSTANT("CURLINFO_CONTENT_TYPE", CURLINFO_CONTENT_TYPE),
   MAKE_ICONSTANT("CURLINFO_REDIRECT_TIME", CURLINFO_REDIRECT_TIME),
   MAKE_ICONSTANT("CURLINFO_REDIRECT_COUNT", CURLINFO_REDIRECT_COUNT),
   MAKE_ICONSTANT("CURLINFO_PRIVATE", CURLINFO_PRIVATE),
   MAKE_ICONSTANT("CURLINFO_HTTP_CONNECTCODE", CURLINFO_HTTP_CONNECTCODE),
   MAKE_ICONSTANT("CURLINFO_HTTPAUTH_AVAIL", CURLINFO_HTTPAUTH_AVAIL),
   MAKE_ICONSTANT("CURLINFO_PROXYAUTH_AVAIL", CURLINFO_PROXYAUTH_AVAIL),
   MAKE_ICONSTANT("CURLINFO_OS_ERRNO", CURLINFO_OS_ERRNO),
   MAKE_ICONSTANT("CURLINFO_NUM_CONNECTS", CURLINFO_NUM_CONNECTS),
   MAKE_ICONSTANT("CURLINFO_SSL_ENGINES", CURLINFO_SSL_ENGINES),

   MAKE_ICONSTANT("CURLE_OK", CURLE_OK),
   MAKE_ICONSTANT("CURLE_UNSUPPORTED_PROTOCOL", CURLE_UNSUPPORTED_PROTOCOL),
   MAKE_ICONSTANT("CURLE_FAILED_INIT", CURLE_FAILED_INIT),
   MAKE_ICONSTANT("CURLE_URL_MALFORMAT", CURLE_URL_MALFORMAT),
   MAKE_ICONSTANT("CURLE_URL_MALFORMAT_USER", CURLE_URL_MALFORMAT_USER),
   MAKE_ICONSTANT("CURLE_COULDNT_RESOLVE_PROXY", CURLE_COULDNT_RESOLVE_PROXY),
   MAKE_ICONSTANT("CURLE_COULDNT_RESOLVE_HOST", CURLE_COULDNT_RESOLVE_HOST),
   MAKE_ICONSTANT("CURLE_COULDNT_CONNECT", CURLE_COULDNT_CONNECT),
   MAKE_ICONSTANT("CURLE_FTP_WEIRD_SERVER_REPLY", CURLE_FTP_WEIRD_SERVER_REPLY),
   MAKE_ICONSTANT("CURLE_FTP_ACCESS_DENIED", CURLE_FTP_ACCESS_DENIED),
   MAKE_ICONSTANT("CURLE_FTP_USER_PASSWORD_INCORRECT", CURLE_FTP_USER_PASSWORD_INCORRECT),
   MAKE_ICONSTANT("CURLE_FTP_WEIRD_PASS_REPLY", CURLE_FTP_WEIRD_PASS_REPLY),
   MAKE_ICONSTANT("CURLE_FTP_WEIRD_USER_REPLY", CURLE_FTP_WEIRD_USER_REPLY),
   MAKE_ICONSTANT("CURLE_FTP_WEIRD_PASV_REPLY", CURLE_FTP_WEIRD_PASV_REPLY),
   MAKE_ICONSTANT("CURLE_FTP_WEIRD_227_FORMAT", CURLE_FTP_WEIRD_227_FORMAT),
   MAKE_ICONSTANT("CURLE_FTP_CANT_GET_HOST", CURLE_FTP_CANT_GET_HOST),
   MAKE_ICONSTANT("CURLE_FTP_CANT_RECONNECT", CURLE_FTP_CANT_RECONNECT),
   MAKE_ICONSTANT("CURLE_FTP_COULDNT_SET_BINARY", CURLE_FTP_COULDNT_SET_BINARY),
   MAKE_ICONSTANT("CURLE_PARTIAL_FILE", CURLE_PARTIAL_FILE),
   MAKE_ICONSTANT("CURLE_FTP_COULDNT_RETR_FILE", CURLE_FTP_COULDNT_RETR_FILE),
   MAKE_ICONSTANT("CURLE_FTP_WRITE_ERROR", CURLE_FTP_WRITE_ERROR),
   MAKE_ICONSTANT("CURLE_FTP_QUOTE_ERROR", CURLE_FTP_QUOTE_ERROR),
   MAKE_ICONSTANT("CURLE_HTTP_RETURNED_ERROR", CURLE_HTTP_RETURNED_ERROR),
   MAKE_ICONSTANT("CURLE_WRITE_ERROR", CURLE_WRITE_ERROR),
   MAKE_ICONSTANT("CURLE_MALFORMAT_USER", CURLE_MALFORMAT_USER),
   MAKE_ICONSTANT("CURLE_FTP_COULDNT_STOR_FILE", CURLE_FTP_COULDNT_STOR_FILE),
   MAKE_ICONSTANT("CURLE_READ_ERROR", CURLE_READ_ERROR),
   MAKE_ICONSTANT("CURLE_OUT_OF_MEMORY", CURLE_OUT_OF_MEMORY),
   MAKE_ICONSTANT("CURLE_OPERATION_TIMEOUTED", CURLE_OPERATION_TIMEOUTED),
   MAKE_ICONSTANT("CURLE_FTP_COULDNT_SET_ASCII", CURLE_FTP_COULDNT_SET_ASCII),
   MAKE_ICONSTANT("CURLE_FTP_PORT_FAILED", CURLE_FTP_PORT_FAILED),
   MAKE_ICONSTANT("CURLE_FTP_COULDNT_USE_REST", CURLE_FTP_COULDNT_USE_REST),
   MAKE_ICONSTANT("CURLE_FTP_COULDNT_GET_SIZE", CURLE_FTP_COULDNT_GET_SIZE),
   MAKE_ICONSTANT("CURLE_HTTP_RANGE_ERROR", CURLE_HTTP_RANGE_ERROR),
   MAKE_ICONSTANT("CURLE_HTTP_POST_ERROR", CURLE_HTTP_POST_ERROR),
   MAKE_ICONSTANT("CURLE_SSL_CONNECT_ERROR", CURLE_SSL_CONNECT_ERROR),
   MAKE_ICONSTANT("CURLE_BAD_DOWNLOAD_RESUME", CURLE_BAD_DOWNLOAD_RESUME),
   MAKE_ICONSTANT("CURLE_FILE_COULDNT_READ_FILE", CURLE_FILE_COULDNT_READ_FILE),
   MAKE_ICONSTANT("CURLE_LDAP_CANNOT_BIND", CURLE_LDAP_CANNOT_BIND),
   MAKE_ICONSTANT("CURLE_LDAP_SEARCH_FAILED", CURLE_LDAP_SEARCH_FAILED),
   MAKE_ICONSTANT("CURLE_LIBRARY_NOT_FOUND", CURLE_LIBRARY_NOT_FOUND),
   MAKE_ICONSTANT("CURLE_FUNCTION_NOT_FOUND", CURLE_FUNCTION_NOT_FOUND),
   MAKE_ICONSTANT("CURLE_ABORTED_BY_CALLBACK", CURLE_ABORTED_BY_CALLBACK),
   MAKE_ICONSTANT("CURLE_BAD_FUNCTION_ARGUMENT", CURLE_BAD_FUNCTION_ARGUMENT),
   MAKE_ICONSTANT("CURLE_BAD_CALLING_ORDER", CURLE_BAD_CALLING_ORDER),
   MAKE_ICONSTANT("CURLE_INTERFACE_FAILED", CURLE_INTERFACE_FAILED),
   MAKE_ICONSTANT("CURLE_BAD_PASSWORD_ENTERED", CURLE_BAD_PASSWORD_ENTERED),
   MAKE_ICONSTANT("CURLE_TOO_MANY_REDIRECTS ", CURLE_TOO_MANY_REDIRECTS ),
   MAKE_ICONSTANT("CURLE_UNKNOWN_TELNET_OPTION", CURLE_UNKNOWN_TELNET_OPTION),
   MAKE_ICONSTANT("CURLE_TELNET_OPTION_SYNTAX ", CURLE_TELNET_OPTION_SYNTAX ),
   MAKE_ICONSTANT("CURLE_OBSOLETE", CURLE_OBSOLETE),
   MAKE_ICONSTANT("CURLE_SSL_PEER_CERTIFICATE", CURLE_SSL_PEER_CERTIFICATE),
   MAKE_ICONSTANT("CURLE_GOT_NOTHING", CURLE_GOT_NOTHING),
   MAKE_ICONSTANT("CURLE_SSL_ENGINE_NOTFOUND", CURLE_SSL_ENGINE_NOTFOUND),
   MAKE_ICONSTANT("CURLE_SSL_ENGINE_SETFAILED", CURLE_SSL_ENGINE_SETFAILED),
   MAKE_ICONSTANT("CURLE_SEND_ERROR", CURLE_SEND_ERROR),
   MAKE_ICONSTANT("CURLE_RECV_ERROR", CURLE_RECV_ERROR),
   MAKE_ICONSTANT("CURLE_SHARE_IN_USE", CURLE_SHARE_IN_USE),
   MAKE_ICONSTANT("CURLE_SSL_CERTPROBLEM", CURLE_SSL_CERTPROBLEM),
   MAKE_ICONSTANT("CURLE_SSL_CIPHER", CURLE_SSL_CIPHER),
   MAKE_ICONSTANT("CURLE_SSL_CACERT", CURLE_SSL_CACERT),
   MAKE_ICONSTANT("CURLE_BAD_CONTENT_ENCODING", CURLE_BAD_CONTENT_ENCODING),
   MAKE_ICONSTANT("CURLE_LDAP_INVALID_URL", CURLE_LDAP_INVALID_URL),
   MAKE_ICONSTANT("CURLE_FILESIZE_EXCEEDED", CURLE_FILESIZE_EXCEEDED),
   MAKE_ICONSTANT("CURLE_FTP_SSL_FAILED", CURLE_FTP_SSL_FAILED),
   MAKE_ICONSTANT("CURLE_SEND_FAIL_REWIND", CURLE_SEND_FAIL_REWIND),
   MAKE_ICONSTANT("CURLE_SSL_ENGINE_INITFAILED", CURLE_SSL_ENGINE_INITFAILED),
#if CURL_VERSION_GE(7,14,0)
   MAKE_ICONSTANT("CURLE_LOGIN_DENIED", CURLE_LOGIN_DENIED),
#endif
   SLANG_END_ICONST_TABLE
};

static void destroy_easy_type (SLtype type, VOID_STAR f)
{
   Easy_Type *ez;
   
   (void) type;
   ez = (Easy_Type *) f;
   free_easy_type (ez);
}

static void destroy_multi_type (SLtype type, VOID_STAR f)
{
   Multi_Type *m;
   
   (void) type;
   m = (Multi_Type *) f;
   free_multi_type (m);
}

#if SLANG_VERSION >= 20005
static int multi_length_method (SLtype type, VOID_STAR v, unsigned int *len)
{
   Multi_Type *m;

   (void) type;
   m = (Multi_Type *) SLang_object_from_mmt (*(SLang_MMT_Type **)v);
   *len = (unsigned int) m->length;
   return 0;
}
#endif

static int register_types (void)
{
   SLang_Class_Type *cl;

   if (Easy_Type_Id == 0)
     {
	if (NULL == (cl = SLclass_allocate_class ("Curl_Type")))
	  return -1;

	if (-1 == SLclass_set_destroy_function (cl, destroy_easy_type))
	  return -1;

	if (-1 == SLclass_register_class (cl, SLANG_VOID_TYPE, sizeof (Easy_Type), SLANG_CLASS_TYPE_MMT))
	  return -1;

	Easy_Type_Id = SLclass_get_class_id (cl);
     }
   
   if (Multi_Type_Id == 0)
     {
	if (NULL == (cl = SLclass_allocate_class ("Curl_Multi_Type")))
	  return -1;

	if (-1 == SLclass_set_destroy_function (cl, destroy_multi_type))
	  return -1;

#if SLANG_VERSION >= 20005
	if (-1 == SLclass_set_length_function (cl, multi_length_method))
	  return -1;
#endif
	if (-1 == SLclass_register_class (cl, SLANG_VOID_TYPE, sizeof (Multi_Type), SLANG_CLASS_TYPE_MMT))
	  return -1;

	Multi_Type_Id = SLclass_get_class_id (cl);
     }

   if (Curl_Error == 0)
     {
	if (-1 == (Curl_Error = SLerr_new_exception (SL_RunTime_Error, "CurlError", "curl error")))
	  return -1;
     }

   return 0;
}

   
int init_curl_module_ns (char *ns_name)
{
   SLang_NameSpace_Type *ns;
   
   if (-1 == register_types ())
     return -1;

   if (NULL == (ns = SLns_create_namespace (ns_name)))
     return -1;

   if ((-1 == SLns_add_intrin_var_table (ns, Module_Variables, NULL))
       || (-1 == SLns_add_intrin_fun_table (ns, Module_Intrinsics, NULL))
       || (-1 == SLns_add_iconstant_table (ns, Module_IConstants, NULL))
       )
     return -1;

   return 0;
}

/* This function is optional */
void deinit_curl_module (void)
{
}
