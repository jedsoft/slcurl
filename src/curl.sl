import ("curl");

$1 = path_concat (path_concat (path_dirname (__FILE__), "help"),
		  "curl.hlp");
if (NULL != stat_file ($1))
  add_doc_file ($1);

provide ("curl");
