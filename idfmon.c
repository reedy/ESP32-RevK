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
#include <sys/ioctl.h>
#include <linux/usbdevice_fs.h>

int
main (int argc, const char *argv[])
{
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

      ioctl(fd, USBDEVFS_RESET, 0);

      int status = 0;
      ioctl (fd, TIOCMGET, &status);
      status |= TIOCM_RTS;      // RTS (low)
      ioctl (fd, TIOCMSET, &status);
      status |= TIOCM_DTR;      // DTR (low)
      ioctl (fd, TIOCMSET, &status);
      status &= ~TIOCM_RTS;     // RTS (high)
      ioctl (fd, TIOCMSET, &status);
      status &= ~TIOCM_DTR;     // DTR (high)
      ioctl (fd, TIOCMSET, &status);

      char line[1024];
      while (1)
      {
         ssize_t l = read (fd, line, sizeof (line) - 1);
         if (l <= 0)
            break;
         line[l] = 0;
         printf ("%s", line);
         if (strstr (line, "invalid header: 0xffffffff"))
            return 0;
      }
      close (fd);
   }
   return 0;
}
