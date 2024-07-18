#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <err.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

int
main (int argc, const char *argv[])
{
   setsid ();
   if (argc <= 1)
      errx (1, "Specify port");
   FILE *f = fopen (argv[1], "r");
   if (!f)
      err (1, "Cannot open %s", argv[1]);
   char *line = NULL;
   size_t len = 0;
   while (1)
   {
      ssize_t l = getline (&line, &len, f);
      if (l <= 0)
         break;
      printf ("%s", line);
      if (strstr (line, "invalid header: 0xffffffff"))
         break;
   }
   free (line);
   killpg (0, SIGTERM);
   fclose (f);
   return 0;
}
