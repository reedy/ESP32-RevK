// Settings generation too

#include <stdio.h>
#include <string.h>
#include <popt.h>
#include <time.h>
#include <sys/time.h>
#include <stdlib.h>
#include <ctype.h>
#include <err.h>

typedef struct def_s def_t;
struct def_s
{
   def_t *next;
   const char *fn;
   char *define;
   char *comment;
   char *type;
   char *name1;
   char *name2;
   char *def;
   char *attributes;
   char *array;
   char config:1;
};
def_t *defs = NULL,
   *deflast = NULL;

int
typename (FILE * O, const char *type)
{
   if (!strcmp (type, "gpio"))
      fprintf (O, "revk_settings_gpio_t");
   else if (!strcmp (type, "binary"))
      fprintf (O, "revk_settings_binary_t*");
   else if (!strcmp (type, "s"))
      fprintf (O, "char*");
   else if (!strcmp (type, "c"))
      fprintf (O, "char");
   else if (*type == 'u' && isdigit ((int) type[1]))
      fprintf (O, "uint%s_t", type + 1);
   else if (*type == 's' && isdigit ((int) type[1]))
      fprintf (O, "int%s_t", type + 1);
   else
      return 1;
   return 0;
}

void
typeinit (FILE * O, const char *type)
{
   fprintf (O, "=");
   if (!strcmp (type, "s"))
      fprintf (O, "\"\"");
   else if (!strcmp (type, "gpio"))
      fprintf (O, "{0}");
   else if (!strcmp (type, "binary"))
      fprintf (O, "NULL");
   else
      fprintf (O, "0");
}

int
main (int argc, const char *argv[])
{
   int debug = 0;
   const char *cfile = "settings.c";
   const char *hfile = "settings.h";
   const char *extension = "def";
   int maxname = 15;
   poptContext optCon;          // context for parsing command-line options
   {                            // POPT
      const struct poptOption optionsTable[] = {
         {"c-file", 'c', POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT, &cfile, 0, "C-file", "filename"},
         {"h-file", 'h', POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT, &hfile, 0, "H-file", "filename"},
         {"extension", 'e', POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT, &extension, 0, "Only handle files ending with this",
          "extension"},
         {"max-name", 0, POPT_ARG_INT | POPT_ARGFLAG_SHOW_DEFAULT, &maxname, 0, "Max name len", "N"},
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

      char *line = NULL;
      size_t len = 0;
      const char *fn;
      while ((fn = poptGetArg (optCon)))
      {
         char *ext = strrchr (fn, '.');
         if (!ext || strcmp (ext + 1, extension))
            continue;
         FILE *I = fopen (fn, "r");
         if (!I)
            err (1, "Cannot open %s", fn);
         while (getline (&line, &len, I) >= 0)
         {
            char *p;
            for (p = line + strlen (line); p > line && isspace (p[-1]); p--);
            *p = 0;
            p = line;
            while (*p && isspace (*p))
               p++;
            if (!*p)
               continue;
            def_t *d = malloc (sizeof (*d));
            memset (d, 0, sizeof (*d));
            d->fn = fn;
            if (*p == '#')
            {
               d->define = p;
            } else if (*p == '/' && p[1] == '/')
            {
               p += 2;
               while (*p && isspace (*p))
                  p++;
               d->comment = p;
            } else
            {
               d->type = p;
               while (*p && !isspace (*p))
                  p++;
               while (*p && isspace (*p))
                  *p++ = 0;
               d->name1 = p;
               while (*p && !isspace (*p) && *p != '.')
                  p++;
               if (*p == '.')
               {
                  *p++ = 0;
                  d->name2 = p;
                  while (*p && !isspace (*p))
                     p++;
               }
               while (*p && isspace (*p))
                  *p++ = 0;
               if (*p)
               {
                  if (*p == '"')
                  {             // Quoted default
                     d->def = p;
                     while (*p && *p != '"');
                     p++;
                     if (*p)
                        *p++ = 0;
                  } else if (*p != '.' && *p != '/' && p[1] != '/')
                  {             // Unquoted default
                     d->def = p;
                     while (*p && !isspace (*p))
                        p++;
                     if (!strncmp (d->def, "CONFIG_", 7))
                        d->config = 1;
                  }
               }
               while (*p && isspace (*p))
                  *p++ = 0;
               if (*p == '.')
               {
                  d->attributes = p;
                  while (*p == '.')
                  {
                     while (*p && !isspace (*p))
                        p++;
                     while (*p && isspace (*p))
                        p++;
                  }
               }
               if (*p == '/' && p[1] == '/')
               {
                  p += 2;
                  while (*p && isspace (*p))
                     p++;
                  d->comment = p;
               }
            }
            if (d->type)
               d->type = strdup (d->type);
            if (d->comment)
               d->comment = strdup (d->comment);
            if (d->define)
               d->define = strdup (d->define);
            if (d->name1)
               d->name1 = strdup (d->name1);
            if (d->name2)
               d->name2 = strdup (d->name2);
            if (d->def)
               d->def = strdup (d->def);
            if (d->attributes)
            {
               char *i = d->attributes,
                  *o = d->attributes;
               while (*i)
               {
                  if (*i == '"' || *i == '\'')
                  {
                     char c = *i;
                     *o++ = *i++;
                     while (*i && *i != c)
                     {
                        if (*i == '\\')
                           *o++ = *i++;
                        *o++ = *i++;
                     }
                  } else if (isspace (*i))
                  {
                     while (*i && isspace (*i))
                        i++;
                     if (*i)
                        *o++ = ',';
                  } else
                     *o++ = *i++;
               }
               *o = 0;
               d->attributes = strdup (d->attributes);
               for (i = d->attributes; *i; i++)
                  if (!strncmp (i, ".array=", 7))
                  {
                     i += 7;
                     for (o = i; *o && *o != ','; o++);
                     d->array = strndup (i, (int) (o - i));
                     break;
                  }
            }
            if (d->type && !d->name1)
               errx (1, "Missing name for %s in %s", d->type, fn);
            if (strlen (d->name1 ? : "") + strlen (d->name2 ? : "") + (d->array ? 1 : 0) > maxname)
               errx (1, "name too long for %s%s in %s", d->name1 ? : "", d->name2 ? : "", fn);
            if (defs)
               deflast->next = d;
            else
               defs = d;
            deflast = d;
         }
         fclose (I);
      }

      FILE *C = fopen (cfile, "w+");
      if (!C)
         err (1, "Cannot open %s", cfile);
      FILE *H = fopen (hfile, "w+");
      if (!H)
         err (1, "Cannot open %s", hfile);

      def_t *d;

      fprintf (C, "// Settings\n");
      fprintf (C, "#include <stdint.h>\n");
      fprintf (C, "#include \"sdkconfig.h\"\n");
      fprintf (C, "#include \"settings.h\"\n");

      fprintf (H, "// Settings\n");
      fprintf (H, "#include <stdint.h>\n");
      fprintf (H, "#include \"esp_system.h\"\n");
      fprintf (H, "typedef struct revk_settings_s revk_settings_t;\n"   //
               "struct revk_settings_s {\n"     //
               " void *ptr;\n"  //
               " const char name[%d];\n"        //
               " const char *def;\n"    //
               " const char *bitfield;\n"       //
               " uint16_t size;\n"      //
               " uint8_t len1:4;\n"     //
               " uint8_t len2:4;\n"     //
               " uint8_t binary:1;\n"   //
               " uint8_t bit;\n"        //
               " uint8_t array;\n"      //
               " uint8_t decimal;\n"    //
               " uint8_t revk:1;\n"     //
               " uint8_t live:1;\n"     //
               " uint8_t fix:1;\n"      //
               " uint8_t set:1;\n"      //
               " uint8_t hex:1;\n"      //
               " uint8_t base64:1;\n"   //
               " uint8_t pass:1;\n"     //
               "};\n", maxname + 1);

      for (d = defs; d && (!d->type || strcmp (d->type, "binary")); d = d->next);
      if (d)
         fprintf (H, "typedef struct revk_settings_binary_s revk_settings_binary_t;\n"  //
                  "struct revk_settings_binary_s {\n"   //
                  " uint16_t len;\n"    //
                  " uint8_t data[];\n"  //
                  "};\n");
      for (d = defs; d && (!d->type || strcmp (d->type, "gpio")); d = d->next);
      if (d)
         fprintf (H, "typedef struct revk_settings_gpio_s revk_settings_gpio_t;\n"      //
                  "struct revk_settings_gpio_s {\n"     //
                  " uint16_t num:12;\n" //
                  " uint16_t inv:1;\n"  //
                  " uint16_t pulldown:1;\n"     //
                  " uint16_t pullup:1;\n"       //
                  " uint16_t set:1;\n"  //
                  "};\n");
      for (d = defs; d && (!d->type || strcmp (d->type, "bit")); d = d->next);
      if (d)
      {                         // Bit fields
         fprintf (H, "enum {\n");
         for (d = defs; d; d = d->next)
            if (d->define)
               fprintf (H, "%s\n", d->define);
            else if (d->type && !strcmp (d->type, "bit"))
               fprintf (H, " REVK_SETTINGS_BITFIELD_%s%s,\n", d->name1, d->name2 ? : "");
         fprintf (H, "};\n");
         fprintf (H, "typedef struct revk_settings_bitfield_s revk_settings_bitfield_t;\n");
         fprintf (H, "struct revk_settings_bitfield_s {\n");
         fprintf (C, "revk_settings_bitfield_t revk_settings_bitfield={0};\n");
         for (d = defs; d; d = d->next)
            if (d->define)
               fprintf (H, "%s\n", d->define);
            else if (d->type && !strcmp (d->type, "bit"))
               fprintf (H, " uint8_t %s%s:1;\n", d->name1, d->name2 ? : "");
         fprintf (H, "};\n");
         for (d = defs; d; d = d->next)
            if (d->define)
               fprintf (H, "%s\n", d->define);
            else if (d->type && !strcmp (d->type, "bit"))
               fprintf (H, "#define	%s%s	revk_settings_bitfield.%s%s\n", d->name1, d->name2 ? : "", d->name1,
                        d->name2 ? : "");
            else if (d->type)
            {
               fprintf (H, "extern ");
               if (typename (H, d->type))
                  errx (1, "Unknown type %s in %s", d->type, d->fn);
               fprintf (H, " %s%s", d->name1, d->name2 ? : "");
               if (d->array)
                  fprintf (H, "[%s]", d->array);
               fprintf (H, ";\n");
            }
         fprintf (H, "extern revk_settings_bitfield_t revk_settings_bitfield;\n");
      }

      fprintf (C, "#define	str(s)	#s\n");
      fprintf (C, "revk_settings_t const revk_settings[]={\n");
      int count = 0;
      for (d = defs; d; d = d->next)
         if (d->define)
            fprintf (C, "%s\n", d->define);
         else if (d->name1)
         {
            count++;
            fprintf (C, " {");
            fprintf (C, ".name=\"%s%s\"", d->name1, d->name2 ? : "");
            fprintf (C, ",.len1=%d", (int) strlen (d->name1));
            if (d->name2)
               fprintf (C, ",.len2=%d", (int) strlen (d->name2));
            if (d->def)
            {
               if (!d->config)
                  fprintf (C, ",.def=\"%s\"", d->def);
               else
                  fprintf (C, ",.def=str(%s)", d->def);
            }
            if (!strcmp (d->type, "bit"))
               fprintf (C, ",.bit=REVK_SETTINGS_BITFIELD_%s%s", d->name1 ? : "", d->name2 ? : "");
            else
            {
               fprintf (C, ",.ptr=&%s%s", d->name1 ? : "", d->name2 ? : "");
               if (strcmp (d->type, "s") && strcmp (d->type, "binary"))
               {
                  fprintf (C, ",.size=sizeof(");
                  typename (C, d->type);
                  fprintf (C, ")");
               }
            }
            if (!strcmp (d->type, "gpio"))
               fprintf (C, ",.fix=1,.set=1,.bitfield=\"↑↓-\"");
            if (!strcmp (d->type, "binary"))
               fprintf (C, ",.binary=1");
            if (d->attributes)
               fprintf (C, ",%s", d->attributes);
            fprintf (C, "},\n");
         }
      fprintf (C, "{0}};\n");
      fprintf (C, "#undef str\n");
      for (d = defs; d; d = d->next)
         if (d->define)
            fprintf (C, "%s\n", d->define);
         else if (d->type && strcmp (d->type, "bit"))
         {
            typename (C, d->type);
            fprintf (C, " %s%s", d->name1 ? : "", d->name2 ? : "");
            if (d->array)
               fprintf (C, "[%s]={0}", d->array);
            else
               typeinit (C, d->type);
            fprintf (C, ";\n");
         }

      fprintf (H, "typedef uint8_t revk_setting_bits_t[%d];\n", (count + 7) / 8);       // Yes, H
      fclose (H);
      fclose (C);
   }
   poptFreeContext (optCon);
   return 0;
}
