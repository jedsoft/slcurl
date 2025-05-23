# -*- sh -*-
#
# To create the SGML files, you will need to install the tm-utils
# package.  See http://www.jedsoft.org/ for more information.
#
TMEXPAND = tmexpand
SL2TM = sl2tm
TM2HLP = $(TMEXPAND) -Mslhlp

MODULE = curl
AUTOGEN_TM = 
MODULE_DEPS = $(HLPFUNS_TM) $(AUTOGEN_TM) local.tm
HLPFUNS_TM = helpfile.tm
HLP_FILE_DEPS = $(MODULE)funs.tm local.tm

TXT_FILES = $(MODULE).txt
SGML_FILES = $(MODULE).sgml
HTML_FILES = $(MODULE).html
TEX_FILES = $(MODULE).tex
PS_FILES = $(MODULE).ps 
PDF_FILES = $(MODULE).pdf
HLP_FILES = $(MODULE).hlp babelfish.hlp

SGML2LATEX = sgml2latex -p letter -o tex
SGML2HTML = sgml2html
SGML2TXT = sgml2txt -f
LATEX = latex
PDFLATEX = pdflatex

TEXTDIR = ../text
PSDIR = ../ps
HTMLDIR = ../html
SGMLDIR = ../sgml
PDFDIR = ../pdf
HELPDIR = ../help

SUBDIRS = $(TEXTDIR) $(HTMLDIR) $(PSDIR) $(SGMLDIR) $(PDFDIR) $(HELPDIR)
SRCDIR = `pwd`

default: $(TXT_FILES) $(HLP_FILES)
all: $(HTML_FILES) $(PDF_FILES) $(TXT_FILES) $(PS_FILES) $(HLP_FILES)
text-files: $(TXT_FILES)
#----- SGML Files -----------------------------------------------------------
$(MODULE).sgml : $(MODULE).tm $(MODULE_DEPS)
	$(TMEXPAND) $(MODULE).tm $(MODULE).sgml
#----- HTML Files -----------------------------------------------------------
$(MODULE).html : $(MODULE).sgml
	$(SGML2HTML) $(MODULE).sgml
#----- TeX Files ------------------------------------------------------------
$(MODULE).tex : $(MODULE).sgml
	$(SGML2LATEX) $(MODULE).sgml
	jed -script ./fixtex.sl $(MODULE).tex
#----- PDF Files -----------------------------------------------------------
$(MODULE).pdf : $(MODULE).tex
	$(MAKE) texclean
	$(PDFLATEX) $(MODULE).tex
	$(PDFLATEX) $(MODULE).tex
	$(PDFLATEX) $(MODULE).tex
#----- PS Files -----------------------------------------------------------
$(MODULE).ps : $(MODULE).tex texclean
	$(LATEX) $(MODULE).tex
	$(LATEX) $(MODULE).tex
	$(LATEX) $(MODULE).tex
	dvips -o $(MODULE).ps $(MODULE).dvi
#----- Text Files -----------------------------------------------------------
$(MODULE).txt: $(MODULE).sgml
	$(SGML2TXT) $(MODULE).sgml
#----------------------------------------------------------------------------
help-files: $(HLP_FILES)
%.hlp: %.tm $(HLP_FILE_DEPS)
	$(TMEXPAND) -Mslhlp $< $@
babelfish.tm: ../../src/babelfish.sl
	tm-strip $< > $@
texclean:
	-rm -f *.dvi *.log *.aux *.toc *.out
clean: texclean
	-rm -f *~ rtl/*.BAK rtl/*~ *.tmp *-error
distclean: clean
	-rm -f *.html *.ps $(HLP_FILE) $(TXT_FILES) $(TEX_FILES) $(SGML_FILES) $(PDF_FILES) $(AUTOGEN_TM)
install-txt: $(TXT_FILES)
	-mkdir $(TEXTDIR)
	-mv $(TXT_FILES) $(TEXTDIR)
install-help: $(HLP_FILES)
	-mkdir -p $(HELPDIR)
	-mv $(HLP_FILES) $(HELPDIR)
install-all: all install-help install-txt $(PS_FILES) $(PDF_FILES)
	-mkdir -p $(HTMLDIR) $(PSDIR) $(PDFDIR)
	-mv *.html $(HTMLDIR)
	-mv $(PS_FILES) $(PSDIR)
	-mv $(PDF_FILES) $(PDFDIR)
install: install-txt install-help
