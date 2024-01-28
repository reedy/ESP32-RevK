// Settings generation too
#define _GNU_SOURCE
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
   int group;
   int decimal;
   char *name;
   char *name1;
   char *name2;
   char *def;
   char *attributes;
   char *array;
   char config:1;               // Is CONFIG_... def
   char quoted:1;               // Is quoted def
};
def_t *defs = NULL,
   *deflast = NULL;

int groups = 0;
char **group = NULL;

int
typename (FILE * O, const char *type)
{
   if (!strcmp (type, "gpio"))
      fprintf (O, "revk_settings_gpio_t");
   else if (!strcmp (type, "blob"))
      fprintf (O, "revk_settings_blob_t*");
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
   if (!strcmp (type, "gpio"))
      fprintf (O, "{0}");
   else if (!strcmp (type, "blob") || !strcmp (type, "s"))
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

      FILE *C = fopen (cfile, "w+");
      if (!C)
         err (1, "Cannot open %s", cfile);
      FILE *H = fopen (hfile, "w+");
      if (!H)
         err (1, "Cannot open %s", hfile);

      fprintf (C, "// Settings\n");
      fprintf (C, "// Generated from:-\n");

      fprintf (H, "// Settings\n");
      fprintf (H, "// Generated from:-\n");

      char *line = NULL;
      size_t len = 0;
      const char *fn;
      while ((fn = poptGetArg (optCon)))
      {
         char *ext = strrchr (fn, '.');
         if (!ext || strcmp (ext + 1, extension))
            continue;
         fprintf (C, "// %s\n", fn);
         fprintf (H, "// %s\n", fn);
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
                     d->quoted = 1;
                     p++;
                     d->def = p;
                     while (*p && *p != '"')
                        p++;
                     if (*p)
                        *p++ = 0;
                  } else if (*p != '.' && *p != '/' && p[1] != '/')
                  {             // Unquoted default
                     d->def = p;
                     while (*p && !isspace (*p))
                        p++;
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
            {
               d->def = strdup (d->def);
               if (!d->quoted && !strncmp (d->def, "CONFIG_", 7))
               {
                  char *p = d->def + 7;
                  while (*p && (isalnum (*p) || *p == '_'))
                     p++;
                  if (!*p)
                     d->config = 1;
               }
            }
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
                        if (*i == '\\' && i[1])
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
                  if (*i == '"')
                  {
                     i++;
                     while (*i && *i != '"')
                     {
                        if (*i == '\\' && i[1])
                           i++;
                        i++;
                     }
                  } else if (!strncmp (i, ".array=", 7))
                  {
                     i += 7;
                     for (o = i; *o && *o != ','; o++);
                     d->array = strndup (i, (int) (o - i));
                  } else if (!strncmp (i, ".decimal=", 9))
                  {
                     i += 9;
                     d->decimal = atoi (i);
                     warnx ("here %s", i);
                  }
            }
            if (d->type && !d->name1)
               errx (1, "Missing name for %s in %s", d->type, fn);
            if (strlen (d->name1 ? : "") + strlen (d->name2 ? : "") + (d->array ? 1 : 0) > maxname)
               errx (1, "name too long for %s%s in %s", d->name1 ? : "", d->name2 ? : "", fn);
            if (d->name1)
            {
               asprintf (&d->name, "%s%s", d->name1, d->name2 ? : "");
               for (def_t * q = defs; q; q = q->next)
                  if (q->name && !strcmp (q->name, d->name))
                     errx (1, "Duplicate %s (%s/%s)", d->name, d->fn, q->fn);
               if (d->name2)
               {
                  int g;
                  for (g = 0; g < groups && strcmp (group[g], d->name1); g++);
                  if (g == groups)
                  {
                     groups++;
                     group = realloc (group, sizeof (*group) * groups);
                     group[g] = d->name1;
                  }
                  d->group = g + 1;     // 0 means not group
               }
            }
            if (defs)
               deflast->next = d;
            else
               defs = d;
            deflast = d;
         }
         fclose (I);
      }

      def_t *d;

      fprintf (C, "\n");
      fprintf (C, "#include <stdint.h>\n");
      fprintf (C, "#include \"sdkconfig.h\"\n");
      fprintf (C, "#include \"settings.h\"\n");

      fprintf (H, "\n");
      fprintf (H, "#include <stdint.h>\n");
      fprintf (H, "#include <stddef.h>\n");

      fprintf (H, "typedef struct revk_settings_s revk_settings_t;\n"   //
               "struct revk_settings_s {\n"     //
               " void *ptr;\n"  //
               " const char name[%d];\n"        //
               " const char *def;\n"    //
               " const char *bitfield;\n"       //
               " uint16_t size;\n"      //
               " uint8_t group;\n"      //
               " uint8_t len1:4;\n"     //
               " uint8_t len2:4;\n"     //
               " uint8_t blob:1;\n"     //
               " uint8_t bit;\n"        //
               " uint8_t array;\n"      //
               " uint8_t decimal;\n"    //
               " uint8_t sign;\n"       //
               " uint8_t revk:1;\n"     //
               " uint8_t live:1;\n"     //
               " uint8_t fix:1;\n"      //
               " uint8_t set:1;\n"      //
               " uint8_t hex:1;\n"      //
               " uint8_t base64:1;\n"   //
               " uint8_t pass:1;\n"     //
               " uint8_t dq:1;\n"       //
               "};\n", maxname + 1);

      for (d = defs; d && (!d->type || strcmp (d->type, "blob")); d = d->next);
      if (d)
         fprintf (H, "typedef struct revk_settings_blob_s revk_settings_blob_t;\n"      //
                  "struct revk_settings_blob_s {\n"     //
                  " uint16_t len;\n"    //
                  " uint8_t data[];\n"  //
                  "};\n");
      for (d = defs; d && (!d->type || strcmp (d->type, "gpio")); d = d->next);
      if (d)
         fprintf (H, "typedef struct revk_settings_gpio_s revk_settings_gpio_t;\n"      //
                  "struct revk_settings_gpio_s {\n"     //
                  " uint16_t num:10;\n" //
                  " uint16_t strong2:1;\n"      //
                  " uint16_t strong:1;\n"       //
                  " uint16_t pulldown:1;\n"     //
                  " uint16_t nopull:1;\n"       //
                  " uint16_t invert:1;\n"       //
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
               fprintf (H, " REVK_SETTINGS_BITFIELD_%s,\n", d->name);
         fprintf (H, "};\n");
         fprintf (H, "typedef struct revk_settings_bitfield_s revk_settings_bitfield_t;\n");
         fprintf (H, "struct revk_settings_bitfield_s {\n");
         fprintf (C, "revk_settings_bitfield_t revk_settings_bitfield={0};\n");
         for (d = defs; d; d = d->next)
            if (d->define)
               fprintf (H, "%s\n", d->define);
            else if (d->type && !strcmp (d->type, "bit"))
               fprintf (H, " uint8_t %s:1;\n", d->name);
         fprintf (H, "};\n");
         for (d = defs; d; d = d->next)
            if (d->define)
               fprintf (H, "%s\n", d->define);
            else if (d->type && !strcmp (d->type, "bit"))
               fprintf (H, "#define	%s	revk_settings_bitfield.%s\n", d->name, d->name);
            else if (d->type)
            {
               fprintf (H, "extern ");
               if (typename (H, d->type))
                  errx (1, "Unknown type %s in %s", d->type, d->fn);
               fprintf (H, " %s", d->name);
               if (d->array)
                  fprintf (H, "[%s]", d->array);
               fprintf (H, ";\n");
            }
         fprintf (H, "extern revk_settings_bitfield_t revk_settings_bitfield;\n");
      }

      fprintf (C, "#define	str(s)	#s\n");
      fprintf (C, "#define	quote(s)	str(s)\n");
      fprintf (C, "revk_settings_t const revk_settings[]={\n");
      int count = 0;
      for (d = defs; d; d = d->next)
         if (d->define)
            fprintf (C, "%s\n", d->define);
         else if (d->name)
         {
            count++;
            fprintf (C, " {");
            fprintf (C, ".name=\"%s\"", d->name);
            if (d->group)
               fprintf (C, ",.group=%d", d->group);
            fprintf (C, ",.len1=%d", (int) strlen (d->name1));
            if (d->name2)
               fprintf (C, ",.len2=%d", (int) strlen (d->name2));
            else
               for (int g = 0; g < groups; g++)
                  if (!strcmp (d->name1, group[g]))
                     errx (1, "Clash %s in %s with sub object", d->name1, d->fn);
            if (d->def)
            {
               if (!d->config)
                  fprintf (C, ",.def=\"%s\"", d->def);
               else
                  fprintf (C, ",.dq=1,.def=quote(%s)", d->def); // Always re quote, string def parsing assumes "
            }
            if (!strcmp (d->type, "bit"))
               fprintf (C, ",.bit=REVK_SETTINGS_BITFIELD_%s", d->name);
            else
            {
               fprintf (C, ",.ptr=&%s", d->name);
               if (strcmp (d->type, "s") && strcmp (d->type, "blob"))
               {
                  fprintf (C, ",.size=sizeof(");
                  typename (C, d->type);
                  fprintf (C, ")");
               }
            }
            if (!strcmp (d->type, "gpio"))
            {
               if (!d->attributes || !strstr (d->attributes, ".fix="))
                  fprintf (C, ",.fix=1");
               fprintf (C, ",.set=1,.bitfield=\"- ~↓↕⇕\"");
            }
            if (*d->type == 's' && isdigit (d->type[1]))
               fprintf (C, ",.sign=1");
            if (!strcmp (d->type, "blob"))
               fprintf (C, ",.blob=1");
            if (d->attributes)
               fprintf (C, ",%s", d->attributes);
            fprintf (C, "},\n");
	    if(d->array&&!strcmp(d->type,"bit"))errx(1,"Cannot do bit array %s in %s",d->name,d->fn);
         }
      fprintf (C, "{0}};\n");
      fprintf (C, "#undef quote\n");
      fprintf (C, "#undef str\n");
      for (d = defs; d; d = d->next)
         if (d->define)
            fprintf (C, "%s\n", d->define);
         else if (d->type && strcmp (d->type, "bit"))
         {
            typename (C, d->type);
            fprintf (C, " %s", d->name);
            if (d->array)
               fprintf (C, "[%s]={0}", d->array);
            else
               typeinit (C, d->type);
            fprintf (C, ";\n");
         }
      // Final includes
      for (d = defs; d; d = d->next)
         if (d->name && d->decimal)
         {
            fprintf (H, "#define	%s_scale	1", d->name);
            for (int i = 0; i < d->decimal; i++)
               fputc ('0', H);
            fprintf (H, "\n");
         }
      fprintf (H, "typedef uint8_t revk_setting_bits_t[%d];\n", (count + 7) / 8);
      fprintf (H, "typedef uint8_t revk_setting_group_t[%d];\n", (groups + 8) / 8);
      fclose (H);
      fclose (C);
   }
   poptFreeContext (optCon);
   return 0;
}
