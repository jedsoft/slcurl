#d curlapi#1:2 \url{http://curl.haxx.se/libcurl/c/$1.html}{\ifarg2{$1}{$2}}

\function{curl_new}
\synopsis{Instantiate a new Curl_Type object}
\usage{Curl_Type curl_new(String_Type url)}
\description
  This function instantiates and returns a \dtype{Curl_Type} and
  returns it.  It is a wrapper around the \cURL library function
  \exmp{curl_easy_init}.  The \dtype{Curl_Type} object is created with
  the \icon{CURLOPT_NOPROGRESS} option set to 1, and
  \icon{CURLOPT_VERBOSE} set to 0.
  
  Upon failure, the function throws a \exc{CurlError} exception.
\seealso{curl_setopt, curl_multi_new, curl_perform}
\done

\function{curl_setopt}
\synopsis{Set an option for a specified Curl_Type object}
\usage{curl_setopt (Curl_Type c, Int_Type opt, ...)}
\description
  The \ifun{curl_setopt} function is a wrapper around the \cURL
  library function \curlapi{curl_easy_setopt}.  For more information
  about the options that this function takes, see
  \curlapi{curl_easy_setopt}.  Only the differences between the
  module function and the corresponding API function will be explained
  here.

  The current version of the \module{curl} module does not support the
  \icon{CURLOPT_*DATA} options.  Instead, support for these options
  has been integrated into the corresponding callback functions.  For
  example, the \icon{CURLOPT_WRITEDATA} option has been merged with
  the \icon{CURLOPT_WRITEFUNCTION}.  Moreover the prototypes for the
  callbacks have been changed to simplify their use, as described below.
\begin{descrip}
\tag{CURLOPT_WRITEFUNCTION} This option requires two parameters: a
  reference to the callback function, and a user-defined object to
  pass to that function.  The callback function will be passed two
  arguments: the specified user-defined object, and a binary string to
  write.  Upon failure, the function must return -1, and any other
  value will indicate success.
\tag{CURLOPT_READFUNCTION} This option requires two parameters: a
  reference to the callback function, and a user-defined object to
  pass to that function.  The callback function will be passed two
  arguments: the specified user-defined object, and the maximum number
  of bytes to be read by the function.  Upon success, the function
  should return a (binary) string, otherwise it should return \NULL to
  indicate failure.
\tag{CURLOPT_WRITEHEADER} This option requires two parameters: a
  reference to the callback function, and a user-defined object to
  pass to that function.  The callback function will be passed two
  arguments: the specified user-defined object, and a header string
  write.  The function must return -1 upon failure, or any other
  integer to indicate success.
\tag{CURLOPT_PROGRESSFUNCTION} This option requires two parameters: a
  reference to the callback function, and a user-defined object to
  pass to that function.  The callback function will be passed five
  arguments: the specified user-defined object, and four double
  precision floating point values that represent the total number of
  bytes to be downloaded, the total downloaded so far, the total to be
  uploaded, and the total currently uploaded.  This function must
  return 0 to indicate success, or non-zero to indicate failure.
\end{descrip}

  A number of the options in the \cURL API take a linked list of
  strings.  Instead of a linked list, the module requires an array of
  strings for such options, e.g.,
#v+
    curl_setopt (c, CURLOPT_HTTPHEADER, 
                 ["User-Agent: S-Lang curl module",
                  "Content-Type: text/xml; charset=UTF-8",
                  "Content-Length: 1234"]);
#v-
\example
  The following example illustrates how to write the contents of a
  specified URL to a file, its download progress to \icon{stdout}, and
  the contents of its header to variable:
#v+
    define write_callback (fp, str)
    {
       return fputs (str, fp);
    }
    define progress_callback (fp, dltotal, dlnow, ultotal, ulnow)
    {
       if (-1 == fprintf (fp, "Bytes Received: %d\n", int(dlnow)))
         return -1;
       if (dltotal > 0.0)
         {
            if (-1 == fprintf (fp, "Percent Received: %g\n",
                               dlnow/dltotal * 100.0))
              return -1;
         }
       return 0;
    }
    define header_callback (strp, str)
    {
       @strp += str;
       return 0;
    }
    define download_url (url, file)
    {
       variable fp = fopen (file, "w");
       variable c = curl_new (url);
       curl_setopt (c, CURLOPT_FOLLOWLOCATION);
       curl_setopt (c, CURLOPT_WRITEFUNCTION, &write_callback, fp);
       curl_setopt (c, CURLOPT_PROGRESSFUNCTION, &progress_callback, stdout);
       variable var = "";
       curl_setopt (c, CURLOPT_HEADERFUNCTION, &header_callback, &var);
       curl_perform (c);
       () = fprintf (stdout, "Header: %s\n", var);
    }
#v-
\seealso{curl_new, curl_perform, curl_multi_new}
\done

\function{curl_global_init}
\synopsis{Initialize the Curl library}
\usage{curl_global_init (flags)}
\description
  This function is a wrapper around the corresponding \cURL library
  function.  See \curlapi{curl_global_init}{its documentation} for
  more information.
\seealso{curl_global_cleanup}
\done

\function{curl_global_cleanup}
\synopsis{Finalize the Curl library}
\usage{curl_global_cleanup}
\description
  This function is a wrapper around the corresponding \cURL library
  function.  See \curlapi{curl_global_cleanup}{its documentation} for
  more information.
\seealso{curl_global_init}
\done

\function{curl_perform}
\synopsis{Transfer a file}
\usage{curl_perform}
\description
  This function is a wrapper around the \curlapi{curl_easy_perform}
  \cURL library function.  See \curlapi{curl_easy_perform}{its
  documentation} for more information.
\seealso{curl_new, curl_setopt, curl_multi_perform}
\done

\function{curl_get_info}
\synopsis{Get information about a Curl_Type object}
\usage{info = curl_get_info (Curl_Type c, Int_Type type)}
\description
  This function returns information of the requested type from a
  \dtype{Curl_Type} object.   The data returned depends upon the value
  of the \exmp{type} argument.  For more information, see see the
  documentation for the \cURL library \curlapi{curl_easy_getinfo} function.
\example
  This example shows how to use the \ifun{curl_get_info} function to
  obtain the effective URL used for the transfer.
#v+
    url = curl_get_info (c, CURLINFO_EFFECTIVE_URL);
#v-
\seealso{curl_new, curl_multi_info_read}
\done

\function{curl_close}
\synopsis{Close a Curl_Type object}
\usage{curl_close}
\description
  This function is a wrapper around the \curlapi{curl_easy_cleanup}
  \cURL library function.  See \curlapi{curl_easy_perform}{its
  documentation} for more information.
\notes
  Normally there is no need to call this function because the module
  automatically frees any memory associated with the \dtype{Curl_Type}
  object when it is no longer referenced.
\seealso{curl_new}
\done

\function{curl_easy_strerror}
\synopsis{Get the string representation for a curl error code}
\usage{String_Type curl_easy_strerror (errcode)}
\description
  This function is a wrapper around the \curlapi{curl_easy_strerror}
  \cURL library function.  See \curlapi{curl_easy_strerr}{its
  documentation} for more information.
\seealso{curl_perform, curl_multi_info_read, curl_multi_perform}
\done

\function{curl_strerror}
\synopsis{Get the string representation for a curl error code}
\usage{String_Type curl_strerror (errcode)}
\description
  This function is a wrapper around the \curlapi{curl_easy_strerror}
  \cURL library function.  See \curlapi{curl_easy_strerr}{its
  documentation} for more information.
\seealso{curl_perform, curl_multi_info_read, curl_multi_perform}
\done

\function{curl_multi_new}
\synopsis{Instantiate a new Curl_Multi_Type object}
\usage{Curl_Multi_Type curl_multi_new ()}
\description
  This function is a wrapper around the \curlapi{curl_multi_init}
  \cURL library function.  It creates a new instance of a
  \dtype{Curl_Multi_Type} object and returns it.
\seealso{curl_multi_perform, curl_setopt}
\done

\function{curl_multi_perform}
\synopsis{Process a Curl_Multi_Type object}
\usage{Int_Type curl_multi_perform (Curl_Multi_Type m [,Double_Type dt])}
\description
  This function is a wrapper around the \curlapi{curl_multi_perform}
  \cURL library function.  However, the \module{curl} module function
  takes an additional argument (\exmp{dt}) that causes the function to
  wait up to that many seconds for one of the underlying
  \dtype{Curl_Type} objects to become ready for reading or writing.
  The function returns the number of \dtype{Curl_Type}.
\seealso{curl_multi_new, curl_multi_length, curl_multi_add_handle}
\done

\function{curl_multi_remove_handle}
\synopsis{Remove a Curl_Type object from a Curl_Multi_Type}
\usage{curl_multi_remove_handle (Curl_Multi_Type m, Curl_Type c)}
\description
  This function removes the specified \dtype{Curl_Type} object from
  the \dtype{Curl_Multi_Type} object.  For more information, see the
  \curlapi{curl_multi_remove_handle}{documentation} for the
  corresponding \cURL library function.
\seealso{curl_multi_add_handle, curl_multi_new, curl_multi_perform}
\done


\function{curl_multi_add_handle}
\synopsis{Add a Curl_Type object to a Curl_Multi_Type}
\usage{curl_multi_add_handle}
\description
  This function adds the specified \dtype{Curl_Type} object to
  the \dtype{Curl_Multi_Type} object.  For more information, see the
  \curlapi{curl_multi_remove_handle}{documentation} for the
  corresponding \cURL library function.
\seealso{curl_multi_remove_handle, curl_multi_new, curl_multi_perform}
\done

\function{curl_multi_close}
\synopsis{Close a Curl_Multi_Type object}
\usage{curl_multi_close (Curl_Multi_Type m)}
\description
  This function is a wrapper around the \curlapi{curl_multi_cleanup}
  \cURL library function.   Any \dtype{Curl_Multi_Type} objects
  associated with the specified \dtype{Curl_Multi_Type} object will be
  removed from it.
\seealso{curl_multi_new, curl_multi_remove_handle, curl_multi_info_read}
\done

\function{curl_multi_info_read}
\synopsis{Get information about a Curl_Multi_Type transfer}
\usage{Curl_Type curl_multi_info_read (Curl_Multi_Type m [,Ref_Type info])}
\description
  This function retrieves information from the specified
  \dtype{Curl_Multi_Type} object about an individual completed
  transfer by one of its associated \dtype{Curl_Type} objects.  If all
  of the associated \dtype{Curl_Type} objects are still being
  processed, the function will return \NULL.  Otherwise it returns the
  completed \dtype{Curl_Type} object.  If an optional \dtype{Ref_Type}
  parameter is passed to the function, then upon return the
  the associated variable be set to an integer representing the
  completion status of the \dtype{Curl_Type} object.  If the
  completion status is 0, then the transfer was successful, otherwise
  the individual transfer failed and the completion status gives the
  error code associated with the transfer.  More infomation about the
  transfer may be obtained by calling the \ifun{curl_get_info} function.
\example
  The \ifun{curl_multi_info_read} function should be called after a
  call to \ifun{curl_multi_perform} has indicated that a transfer has
  taken place.  The following example repeatedly calls
  \ifun{curl_multi_info_read} until it returns \NULL.  Each time
  through the loop, the completed \dtype{Curl_Type} object is removed
  from the \dtype{Curl_Multi_Type} object.
#v+
        while (c = curl_multi_info_read (m, &status), c!=NULL)
          {
             curl_multi_remove_handle (m, c);
             url = curl_get_url (c);
             if (status == 0)
               vmessage ("Retrieved %s", url);
             else
               vmessage ("Unable to retrieve %s: Reason %s", 
                          url, curl_strerr (status));
          }
#v-
\seealso{curl_multi_perform, curl_multi_remove_handle, curl_get_info}
\done

\function{curl_get_url}
\synopsis{Get the URL associated with a Curl_Type object}
\usage{String_Type curl_get_url (Curl_Type c)}
\description
  This function returns the name of the URL that was used to
  instantiate the specified \dtype{Curl_Type} object.
\seealso{curl_new, curl_get_info}
\done

\function{curl_multi_length}
\synopsis{Get the number of Curl_Type objects in a Curl_Multi_Type}
\usage{Int_Type curl_multi_length (Curl_Multi_Type m)}
\description
  This function returns the number of \dtype{Curl_Type} objects
  contained in the specified \dtype{Curl_Multi_Type} object.
\seealso{curl_multi_remove_handle, curl_multi_add_handle}
\done


