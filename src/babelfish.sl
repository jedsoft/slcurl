% Copyright (C) 2005-2010 John E. Davis

% This file is part of the S-Lang Curl Module

% This script is free software; you can redistribute it and/or
% modify it under the terms of the GNU General Public License as
% published by the Free Software Foundation; either version 2 of the
% License, or (at your option) any later version.

% The script is distributed in the hope that it will be useful,
% but WITHOUT ANY WARRANTY; without even the implied warranty of
% MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
% General Public License for more details.

% You should have received a copy of the GNU General Public License
% along with this library; if not, write to the Free Software
% Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307,
% USA.

require ("curl");

$1 = path_concat (path_concat (path_dirname (__FILE__), "help"),
		  "babelfish.hlp");
if (NULL != stat_file ($1))
  add_doc_file ($1);

private variable Supported_Translations =
  ["zh_en", "zh_zt", "zt_en", "zt_zh", "en_zh", "en_zt", "en_nl", "en_fr",
   "en_de", "en_el", "en_it", "en_ja", "en_ko", "en_pt", "en_ru", "en_es",
   "nl_en", "nl_fr", "fr_nl", "fr_en", "fr_de", "fr_el", "fr_it", "fr_pt",
   "fr_es", "de_en", "de_fr", "el_en", "el_fr", "it_en", "it_fr", "ja_en",
   "ko_en", "pt_en", "pt_fr", "ru_en", "es_en", "es_fr"];

private variable Languages = Assoc_Type[String_Type];
private define add_language (lang, desc)
{
   Languages[strlow(lang)] = strlow (desc);
}
add_language ("zh", "Chinese-simple");
add_language ("zt", "Chinese-traditional");
add_language ("en", "English");
add_language ("nl", "Dutch");
add_language ("fr", "French");
add_language ("de", "German");
add_language ("el", "Greek");
add_language ("it", "Italian");
add_language ("ja", "Japanese");
add_language ("ko", "Korean");
add_language ("pt", "Portugese");
add_language ("ru", "Russian");
add_language ("es", "Spanish");

private define lookup_language (lang)
{
   lang = strlow (lang);
   if (assoc_key_exists (Languages, lang))
     return lang;
   variable vals = assoc_get_values (Languages);
   variable i = where (vals == lang);
   if (length (i) == 0)
     throw NotImplementedError, "Language $lang is unknown or unsupported"$;

   return assoc_get_keys (Languages)[i[0]];
}

private define lookup_translation (from, to)
{
   from = lookup_language (from);
   to = lookup_language (to);
   variable trans = sprintf ("%s_%s", from, to);

   if (any (Supported_Translations == trans))
     return trans;

   throw NotImplementedError, "Translating from $from to $to is not supported"$;
}

private define parse_output (str)
{
   %() = fputs (str, stdout);

   (str,) = strreplace (str, "\n", "\x01", strbytelen (str));
   % Look for TEXT in
   % <div id="result"><div ...>TEXT</div>
   variable start_re = "<div id=\"result\"><[^>]+>";
   variable end_re = "</div>";
   variable re = strcat (start_re, "\([^<]+\)"R, end_re);

   variable n = string_match (str, re, 1);
   if (n == 0)
     return "";
   variable pos, match_len;
   (pos, match_len) = string_match_nth (1);
   str = substrbytes (str, pos+1, match_len);
   variable len = strbytelen (str);
   (str,) = strreplace (str, "\x01", "\n", len);
   ifnot (is_substr (str, "&"))
     return str;

   variable substrs = strchop (str, '&', 0);
   _for (0, length (substrs)-1, 1)
     {
	variable i, j;
	i = ();
	variable ss = substrs[i];
	if (ss[0] == '#')
	  {
	     variable x;
	     if (2 == sscanf (ss, "#%d;%n", &x, &j))
	       {
		  % Look for "#xyz;..."
		  ss = char(x) + ss[[j:]];
		  substrs[i] = ss;
		  continue;
	       }
	  }
     }
   str = strjoin (substrs, "");
   return str;
}

private define write_callback (vp, data)
{
   @vp = strcat (@vp, data);
   return 0;
}

define babelfish ()
%!%+
%\function{babelfish}
%\synopsis{Lookup an online translation by Yahoo! Babel Fish}
%\usage{String_Type babelfish (String_Type lang_from, lang_to, text)}
%\description
%  The languages \exmp{lang_from} and \exmp{lang_to} can be from the
%  following list, but not all combinations are supported:
%#v+
%     "zh" or "Chinese-simp"
%     "zt" or "Chinese-trad"
%     "nl" or "Dutch"
%     "en" or "English"
%     "fr" or "French"
%     "de" or "German"
%     "el" or "Greek"
%     "it" or "Italian"
%     "ja" or "Japanese"
%     "ko" or "Korean"
%     "pt" or "Portuguese"
%     "ru" or "Russian"
%     "es" or "Spanish"
%#v-
%!%-
{
   if(_NARGS!=3)  usage("%s (from, to, text)", _function_name());
   variable from, to, text;  (from, to, text) = ();

   %() = fprintf (stderr, "Input: %s\n", text);
   %variable c = curl_new ("http://babelfish.altavista.com/babelfish/tr?il=en");
   variable c = curl_new ("http://babelfish.yahoo.com/translate_txt");
   variable trans = lookup_translation (from, to);
   variable postdata =
     strcat (%"ei=ISO-8859-1",
	     "ei=UTF-8",
	     "&doit=done",
	     "&fr=bf-res",
	     "&intl=1",
	     "&tt=urltext",
	     "&trtext=", text,
	     "&lp=", trans,
	     "&btnTrTxt=Translate");

   curl_setopt (c, CURLOPT_POSTFIELDS, postdata);
   curl_setopt (c, CURLOPT_FOLLOWLOCATION);
   curl_setopt (c, CURLOPT_HTTPHEADER,
		["User-Agent: S-Lang cURL Module",
		 "Content-Type: application/x-www-form-urlencoded",
		 %"Accept-Charset: ISO-8859-1,utf-8"
		 "Accept-Charset: utf-8"
		 ]);
   text = "";
   curl_setopt (c, CURLOPT_WRITEFUNCTION, &write_callback, &text);
   curl_perform (c);
   text = parse_output (text);
   return text;
}

provide ("babelfish");


#ifdefined gencode  % generate code with `slsh -Dgencode babelfish.sl'

variable c = curl_new("http://babelfish.yahoo.com/translate_txt");
variable html = "";
curl_setopt(c, CURLOPT_WRITEFUNCTION, &write_callback, &html);
curl_perform(c);
variable tr={}, lang=Assoc_Type[String_Type];
foreach html (strchop(html, '\n', 0))
{
  variable m = string_matches(html, `<option value="\(..\)_\(..\)">\(.*\) to \(.*\)</option>`);
  if(m!=NULL)
  {
    list_append(tr, sprintf(`"%s_%s"`, m[1], m[2]));
    lang[ m[1] ] = m[3];
    lang[ m[2] ] = m[4];
  }
}
tr = list_to_array(tr);
tr[[:-2]] += [", ", ",\n   "][ [0:length(tr)-2] mod 8==7];
vmessage("private variable Supported_Translations =\n  [%s];\n", strjoin(tr));

variable k = assoc_get_keys(lang);
variable v = assoc_get_values(lang);
foreach lang ( array_sort(v) )
  vmessage(`add_language ("%s", "%s");`, k[lang], v[lang]);

#endif
