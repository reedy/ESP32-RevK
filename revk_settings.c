// Settings generation too

#include <stdio.h>
#include <string.h>
#include <popt.h>
#include <time.h>
#include <sys/time.h>
#include <stdlib.h>
#include <ctype.h>
#include <err.h>


int
main (int argc, const char *argv[])
{
int debug = 0;
	const char *cfile="settings.c";
	const char *hfile="settings.h";
   poptContext optCon;          // context for parsing command-line options
   {                            // POPT
      const struct poptOption optionsTable[] = {
      {"c-file", 'c', POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT, &cfile, 0, "C-file", "filename"},
      {"h-file", 'h', POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT, &hfile, 0, "H-file", "filename"},
         {"debug", 'v', POPT_ARG_NONE, &debug, 0, "Debug"},
         POPT_AUTOHELP {}
      };

      optCon = poptGetContext (NULL, argc, argv, optionsTable, 0);
      poptSetOtherOptionHelp (optCon, "Definitions-files");

      int c;
      if ((c = poptGetNextOpt (optCon)) < -1)
         errx (1, "%s: %s\n", poptBadOption (optCon, POPT_BADOPTION_NOALIAS), poptStrerror (c));

      if (!poptPeekArg (optCon))
      {
         poptPrintUsage (optCon, stderr, 0);
         return -1;
      }


   }
   poptFreeContext (optCon);
   return 0;
}
