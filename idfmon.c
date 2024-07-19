#define _GNU_SOURCE

// Connect to ESP and monitor.
// Keep reconnecting
// Exit and abort when we find it is waiting for code (i.e. invalid header: 0xffffffff)
// Output what it sends otherwise

#include <stdio.h>
#include <stdlib.h>
#include <err.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

int
main (int argc, const char *argv[])
{
   setsid ();
   if (argc <= 1)
      errx (1, "Specify port");

   while (1)
   {
      int fd = open (argv[1], O_RDWR | O_NOCTTY);
      if (fd < 0)
      {
         putchar ('.');
         fflush (stdout);
         sleep (1);
         continue;
      }

      struct termios t;
      tcgetattr (fd, &t);
      cfmakeraw (&t);
      t.c_cflag &= ~CRTSCTS;
      tcsetattr (fd, TCSANOW, &t);

      FILE *f = fdopen (fd, "r");
      char *line = NULL;
      size_t len = 0;
      while (1)
      {
         ssize_t l = getline (&line, &len, f);
         if (l <= 0)
            break;
         printf ("%s", line);
         if (strstr (line, "invalid header: 0xffffffff"))
         {
            killpg (0, SIGTERM);
            fclose (f);
            return 0;
         }
      }
      free (line);
   }
   return 0;
}
