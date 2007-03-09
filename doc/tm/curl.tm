#% -*- mode: tm; mode: fold -*-

#%{{{Macros 

#i linuxdoc.tm
#d it#1 <it>$1</it>

#d slang \bf{S-lang}
#d exmp#1 \tt{$1}
#d var#1 \tt{$1}

#d ivar#1 \tt{$1}
#d ifun#1 \tt{$1}
#d cvar#1 \tt{$1}
#d cfun#1 \tt{$1}
#d svar#1 \tt{$1}
#d sfun#1 \tt{$1}
#d icon#1 \tt{$1}
#d dtype#1 \tt{$1}
#d exc#1 \tt{$1}

#d chapter#1 <chapt>$1<p>
#d preface <preface>
#d tag#1 <tag>$1</tag>

#d function#1 \sect{<bf>$1</bf>\label{$1}}<descrip>
#d variable#1 \sect{<bf>$1</bf>\label{$1}}<descrip>
#d function_sect#1 \sect{$1}
#d begin_constant_sect#1 \sect{$1}<itemize>
#d constant#1 <item><tt>$1</tt>
#d end_constant_sect </itemize>

#d synopsis#1 <tag> Synopsis </tag> $1
#d keywords#1 <tag> Keywords </tag> $1
#d usage#1 <tag> Usage </tag> <tt>$1</tt>
#d description <tag> Description </tag>
#d example <tag> Example </tag>
#d notes <tag> Notes </tag>
#d seealso#1 <tag> See Also </tag> <tt>\linuxdoc_list_to_ref{$1}</tt>
#d done </descrip><p>
#d -1 <tt>-1</tt>
#d 0 <tt>0</tt>
#d 1 <tt>1</tt>
#d 2 <tt>2</tt>
#d 3 <tt>3</tt>
#d 4 <tt>4</tt>
#d 5 <tt>5</tt>
#d 6 <tt>6</tt>
#d 7 <tt>7</tt>
#d 8 <tt>8</tt>
#d 9 <tt>9</tt>
#d NULL <tt>NULL</tt>
#d documentstyle book

#%}}}

#d module#1 \tt{$1}
#d file#1 \tt{$1}
#d slang-documentation \
 \url{http://www.s-lang.org/doc/html/slang.html}{S-Lang documentation}

\linuxdoc
\begin{\documentstyle}

\title S-Lang cURL Module Reference
\author John E. Davis, \tt{jed@jedsoft.org}
\date \__today__

#i local.tm

\toc

\chapter{Introduction to the cURL Module}

  The S-Lang \module{curl} module makes use of the \cURL library to
  provide the \slang interpreter the ability to transfer files in a
  simple but robust manner using a variety of protocols including FTP
  and HTTP.  
  
  Although there are some minor differences, the functions in
  \module{curl} module represent a more or less straightforward mapping
  of the underlying \cURL library.  As such, the user the user is
  strongly encouraged to read the \url{\curldocs}{excellent,
  well-written documentation} for the \cURL library, which even
  includes a tutorial.  Moreover, anyone familiar with the \cURL API
  should have no problems using the \module{curl}.

  Currently the module provides \slang interfaces to the so-called
  \cURL ``easy'' and ``multi'' class of functions, which permit
  sequential and asynchronous file transfers, respectively.

  All functions in the module have names that start with \exmp{curl_}.
  Functions in the ``multi'' interface begin with \exmp{curl_multi_}.
  
  Before the module can be used it must first be loaded into the
  interpreter using a line such as
#v+
    require ("curl");
#v-

\chapter{The Easy Interface}

  The ``easy'' interface consists of the functions:
#v+
    Curl_Type curl_new (String_Type URL);
    curl_perform (Curl_Type obj);
    curl_setopt (Curl_Type obj, Int_Type op, ...);
    curl_close (Curl_Type obj);
#v-
  and allows a \slang script to transfer files is a
  simple synchronous manner.  For example, 
#v+
     curl_perform (curl_new ("http://www.jedsoft.org/slang/"));
#v-
  will cause the contents of \exmp{http://www.jedsoft.org/slang/} to
  be retrieve via the http protocol and then written to the display.

  Note that the above example makes two function calls to the
  \module{curl} module.  The call to \sfun{curl_new} produces an
  instance of a \dtype{Curl_Type} object associated with the
  specified URL.  The resulting \curl object gets passed to the
  \sfun{curl_perform} function to be processed.  In this case, the
  default action of \sfun{curl_perform} causes the URL to be
  downloaded and then written to \ivar{stdout}.

  The \module{curl} handles the closing and destruction of the
  \dtype{Curl_Type} object when the variable attached to it goes
  out of scope or gets reassigned.  Nevertheless the
  \ifun{curl_close} function exists to allow the user to explicitly
  destroy the underlying \dtype{Curl_Type} object.

  The \ifun{curl_setopt} function may be used to set options or
  attributes associated with a \dtype{Curl_Type} object.  Such
  options influence the actions of \sfun{curl_perform}.  For example,
  the \exmp{CURLOPT_WRITEFUNCTION} option may be used to have
  \sfun{curl_perform} pass the contents of the URL to a specified
  function or \em{callback}.  To illustrate this, consider a callback
  that writes the contents of the URL to a file:
#v+
     private define write_callback (fp, data)
     {
        variable len = bstrlen (data);
        if (len != fwrite (data, fp))
          return -1;
        return 0;
     }
#v-
  In this function, \exmp{fp} is assumed to be an open file pointer
  and \exmp{data} is a binary string (\dtype{BString_Type}).  The
  callback returns 0 if it successfully wrote the data and -1 if not.
  Here is how the callback can be used to download a file in MP3 format:
#v+
     variable c = curl_new ("http://some.url.org/quixote.mp3");
     variable fp = fopen ("quixote.mp3", "wb");
     curl_setopt (c, CURLOPT_WRITEFUNCTION, &write_callback, fp);
     curl_perform (c);
     () = fclose (fp);
#v-

  Often one wants to get the contents of the URL in a \slang variable.
  This is easily accomplished via a callback such as:
#v+
     private define write_variable_callback (v, data)
     {
        @v = @v + data;
        return 0;
     }
#v-
  The above callback may be used as follows:
#v+
     variable c = curl_new ("http://some.url.org/quixote.mp3");
     variable s = "";
     curl_setopt (c, CURLOPT_WRITEFUNCTION, &write_variable_callback, &s);
     curl_perform (c);
#v-
  The \ifun{curl_perform} function passes the reference to the
  variable \exmp{s} to the callback function, which then dereferences
  for concatenation.  After the call to \ifun{curl_perform}, \var{s}
  will be set to the contents of the URL.

  Errors are handled via exceptions.  If an error occurs, e.g., a host
  could not be contacted, or the specified URL does not exist, then a
  \exc{CurlError} exception will be thrown.   Here is an example that
  processes a list of URLs and prints an error if one cannot be
  retrieved:
#v+
      urls = {"http://servantes.fictional.domain/don/quixote.html",
              "http://servantes.fictional.domain/sancho/panza.html"};
      foreach url (urls)
       {
         try (e)
            {
               file = path_basename (url);
               fp = fopen (file, "w");
               c = curl_new (url);
               curl_setopt (c, CURLOPT_WRITEFUNCTION, &write_callback, fp);
               curl_perform (c);
               () = fclose (fp);
            }
          catch CurlError:
            {
               vmessage ("Unable to retrieve %s: %s", url, e.message);
               () = remove (file);
            }
       }
#v-

  The URLs in the above example are processed in a sequential manner.
  This example will be revisited in the context of the ``multi''
  interface where it will be rewritten to perform a synchronous
  downloads.
  
\chapter{The Multi Interface}
  
  The ``multi'' interface permits transfers to take place in an
  asynchronous manner.  It consists of the functions:
#v+
    Curl_Multi_Type curl_multi_new ();
    curl_multi_remove_handle (Curl_Multi_Type mobj, Curl_Type obj);
    curl_multi_add_handle (Curl_Multi_Type mobj, Curl_Type obj);
    curl_multi_close (Curl_Multi_Type mobj);
    Int_Type curl_multi_perform (Curl_Multi_Type obj [,Double_Type timeout]);
    curl_multi_info_read(Curl_Multi_Type obj);
#v-

 A \dtype{Curl_Multi_Type} object is essentially a collection of
 \dtype{Curl_Type} objects.  As such one cannot understand the
 multi-interface without understanding the easy-interface.

 As the name suggests, the \ifun{curl_multi_new} function creates an
 instance of a \dtype{Curl_Multi_Type} object.  The
 \ifun{curl_multi_add_handle} function is used to add a
 \dtype{Curl_Type} object to the specified
 \dtype{Curl_Multi_Type}.  Similarly, the
 \exmp{curl_multi_remove_handle} is used to remove a \dtype{Curl_Type}.
 Although the module automatically destroys the underlying
 \dtype{Curl_Multi_Type} object when it goes out of scope, the
 \ifun{curl_multi_close} may be used to explicitly perform this
 operation.

 The \ifun{curl_multi_perform} function is used to carry out the
 actions of the \dtype{Curl_Type} objects associated with the
 \dtype{Curl_Multi_Type} object.  The \ifun{curl_multi_info_read}
 may be used to find the result of the \ifun{curl_multi_perform}
 function.
 
 To illustrate the use of these functions, consider once again the
 last example of the previous section involving the processing of a
 list of URLs.  Here it is again except written to use the ``multi''
 interface:
#v+
{
   urls = {"http://servantes.fictional.domain/don/quixote.html",
           "http://servantes.fictional.domain/sancho/panza.html"};
   fp_list = Assoc_Type[];
   m = curl_multi_new ();
   foreach url (urls)
     {
        file = path_basename (url);
        fp = fopen (file, "w");
        fp_list [url] = fp;
        c = curl_new (url);
        curl_setopt (c, CURLOPT_WRITEFUNCTION, &write_callback, fp);
        curl_multi_add_handle (m, c);
     }
   dt = 5.0;
   while (last_n = curl_multi_length (m), last_n > 0)
     {
        n = curl_multi_perform (m, dt);
        if (n == last_n)
          continue;
        while (c = curl_multi_info_read (m, &status), c!=NULL)
          {
             curl_multi_remove_handle (m, c);
             url = curl_get_url (c);
             () = fclose (fp_list[url]);
             if (status == 0)
               vmessage ("Retrieved %s", url);
             else 
               vmessage ("Unable to retrieve %s", url);
          }
     }
#v-
 The above code fragment consists of two stages:  The first stage
 involves the creation of individual \dtype{Curl_Type} objects via
 \ifun{curl_new} and populating the \ifun{Curl_Multi_Type} object
 assigned to the variable \exmp{m} using the
 the \ifun{curl_multi_add_handle} function.  Also during this stage,
 files were opened and the resulting file pointers were placed in an
 associative array for later use.
 
 The second stage involves a nested ``while'' loop.  The outer loop
 will continue to run as long as there are still \dtype{Curl_Type}
 objects contained in \exmp{m}.  The \exmp{curl_multi_length} function
 returns the number of such objects.  Each time through the loop, the
 \exmp{curl_multi_perform} function is called with a time-out value of
 5.0 seconds.  This means that the function will wait up to 5.0
 seconds for input on one of the underlying curl objects before
 returning.  It returns the number of such objects that are still active.
 If that number is less than the number contained in the multi-type
 object, then at least one of the objects has finished processing.
 
 The inner-loop of the second stage will execute if the transfer of an
 object has taken place.  This loop repeatedly calls the
 \ifun{curl_multi_info_read} to obtain a completed \dtype{Curl_Type}
 object.  In the body of the loop, the object is removed from the
 multi-type and the file associated with the URL is closed.  The
 processing status, which is returned by \ifun{curl_multi_info_read}
 through its argument list, is also checked at this time.
 
 Although this seems like a lot of complexity compared to the ``easy''
 approach taken earlier, the reward is greater.  Since the transfers
 are performed asynchronously, the time spent downloading the entire
 list of URLs can be a fraction of that of the synchronous approach.

\chapter{cURL Module Function Reference}
#i curlfuns.tm

\end{\documentstyle}
