/*
    Copyright (C) 2010-2011  Serge V. Baumer

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, version 3 of the License.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "rssthreads.h"
#include <getopt.h>
#include "help.h"

const char *progname;

#define LONGOPTS_INDEX \
	"AB:CD::F:I:K:L::MN:P::R:T:U:" \
	"a::c::d::e:f:g:hi::l:m:nop:r::st:u:v:x"

static const struct option longopts[] = {
	{"active",        no_argument,         NULL, 'A'},
	{"browser",       required_argument,   NULL, 'B'},
	{"case",          no_argument,         NULL, 'C'},
	{"desc-filter",   optional_argument,   NULL, 'D'},
	{"mark",          required_argument,   NULL, 'F'},
	{"id",            required_argument,   NULL, 'I'},
	{"db-keys",       required_argument,   NULL, 'K'},
	{"print-links",   optional_argument,   NULL, 'L'},
	{"auto-mark",     no_argument,         NULL, 'M'},
	{"interval",      required_argument,   NULL, 'N'},
	{"dump",          optional_argument,   NULL, 'P'},
	{"rec-from",      required_argument,   NULL, 'R'},
	{"description",   required_argument,   NULL, 'T'},
	{"pub-from",      required_argument,   NULL, 'U'},
	{"alter",         optional_argument,   NULL, 'a'},
	{"create",        optional_argument,   NULL, 'c'},
	{"drop",          optional_argument,   NULL, 'd'},
	{"tuple",         required_argument,   NULL, 'e'},
	{"sort-field",    required_argument,   NULL, 'f'},
	{"categories",    required_argument,   NULL, 'g'},
	{"help",          no_argument,         NULL, 'h'},
	{"info",          optional_argument,   NULL, 'i'},
	{"link",          required_argument,   NULL, 'l'},
	{"marked",        required_argument,   NULL, 'm'},
	{"not",           no_argument,         NULL, 'n'},
	{"asc-sort",      no_argument,         NULL, 'o'},
	{"table",         required_argument,   NULL, 'p'},
	{"read",          optional_argument,   NULL, 'r'},
	{"setup",         no_argument,         NULL, 's'},
	{"title",         required_argument,   NULL, 't'},
	{"url",           required_argument,   NULL, 'u'},
	{"verbose",       required_argument,   NULL, 'v'},
	{"show-extra",    no_argument,         NULL, 'x'},
	{NULL,            0,                   NULL, 0}
};

typedef enum {
	setup, create, info, alter, drop, reader,
	dump, links, mark, breakdown, help
} action_var;

unsigned short rssth_verbose = 0;

int main (int argc, char **argv) {
	action_var action = info;
	int opt;
	progname = argv[0];

	setlocale (LC_ALL, "");

	struct selector sel;
	memset (&sel, 0, sizeof(sel));
	sel.dbKeys = "";
	sel.qualifier = qlf_none;
	if (!(sel.browser = getenv("RSSTH_BROWSER")))
		sel.browser = "lynx %s";

	/* several macros for the options parsing loop */

	#define set_action(ACTION) if (action != breakdown) action = ACTION
	#define set_qualifier(QUALIFIER) if (sel.qualifier == qlf_none) \
		sel.qualifier = qlf_ ## QUALIFIER
	#define auto_set(VARNAME) { \
		if (sel.VARNAME) { \
			sel.VARNAME ## 2 = optarg; \
			sel.VARNAME ## 2_auto = 0; \
		} else { \
			sel.VARNAME ## 2 = (sel.VARNAME = optarg); \
			sel.VARNAME ## 2_auto = 1; \
		} \
	}
	#define set_id() { set_qualifier (id); auto_set (id); }
	#define set_link() { set_qualifier (url); auto_set (link); }
	#define set_table() { \
		set_qualifier (table); \
		if (strlen(optarg) > 32) { \
			fprintf (stderr, "%s: table name has length greater than 32: %s", \
					progname, optarg); \
			action = breakdown; \
		} \
		auto_set (table) \
	}
	#define parse_optarg() { \
		if (isncstr (optarg)) \
			set_id() \
		else if (strstr(optarg, "://")) \
			set_link() \
		else \
			set_table() \
	}

	#define set_mark(VALUE) do { \
		switch (*optarg) { \
			case 'r': sel.readMark = VALUE; break; \
			case 'p': sel.primaryMark = VALUE; break; \
			case 's': sel.secondaryMark = VALUE; break; \
			case 'd': sel.deleteMark = VALUE; break; \
		} \
	} while (*(++optarg) != '\0')

	unsigned short inv = 0, cs = 0;
	#define invflag (inv ? inv-- : inv)
	#define csflag (cs ? cs-- : cs)

	while ((opt = getopt_long (argc, argv, LONGOPTS_INDEX,
					longopts, NULL)) != -1) {
		switch (opt) {
			case 's':
				set_action (setup);
				break;
			case 'c':
				set_action (create);
				if (optarg) parse_optarg()
				break;
			case 'i':
				set_action (info);
				if (optarg) parse_optarg()
				break;
			case 'a':
				set_action (alter);
				if (optarg) parse_optarg()
				break;
			case 'd':
				set_action (drop);
				if (optarg) parse_optarg()
				break;
			case 'r':
				set_action (reader);
				if (optarg) parse_optarg()
				break;
			case 'P':
				set_action (dump);
				if (optarg) parse_optarg()
				break;
			case 'L':
				set_action (links);
				if (optarg) parse_optarg()
				break;
			case 'F':
				set_action (mark);
				sel.markSet = optarg;
				break;
			case 'K':
				sel.dbKeys = optarg; break;
			case 'n':
				inv = 1; break;
			case 'C':
				cs = 1; break;
			case 'M':
				if (invflag) sel.noread = 1; break;
			case 'u':
				set_link(); break;
			case 'p':
				set_table() break;
			case 'N':
				sel.interval = optarg; break;
			case 'A':
				if (invflag)
					sel.active = "no";
				else
					sel.active = "yes";
				break;
			case 'I':
				set_id(); break;
			case 't':
				if (invflag) {
					if (csflag)
						sel.title_nomatch_cs = optarg;
					else
						sel.title_nomatch = optarg;
				} else {
					if (csflag)
						sel.title_cs = optarg;
					else
						sel.title = optarg;
				}
				break;
			case 'l':
				set_link(); break;
			case 'U':
				if (invflag) sel.before = 1;
				sel.pubDate = optarg; break;
			case 'R':
				if (invflag) sel.before = 1;
				sel.recDate = optarg; break;
			case 'T':
				if (invflag) {
					if (csflag) 
						sel.description_nomatch_cs = optarg;
					else
						sel.description_nomatch = optarg;
				} else {
					if (csflag)
						sel.description_cs = optarg;
					else
						sel.description = optarg;
				}
				break;
			case 'g':
				if (invflag)
					sel.noCats = optarg;
				else
					sel.cats = optarg;
				break;
			case 'x':
				if (invflag)
					sel.hideExtra = 1;
				else
					sel.hideExtra = 0;
				break;
			case 'm':
				if (invflag) {
					set_mark (-1);
				} else {
					set_mark (1);
				}
				break;
			case 'f':
				sel.sortField = optarg; break;
			case 'o':
				if (invflag)
					sel.sortForward = 0;
				else
					sel.sortForward = 1;
				break;
			case 'e':
				sel.tuple = optarg; break;
			case 'B':
				sel.browser = optarg; break;
			case 'D':
				if (optarg)
					sel.descfilter = optarg;
				else if (!(sel.descfilter = getenv("RSSTH_DESCFILTER")))
					sel.descfilter = "cat";
				break;
			case 'v':
				rssth_verbose = strtoul (optarg, NULL, 0); break;
			//case 'w':
			//	sel.screenWidth = 
			case 'h':
				set_action (help);
				break;
			default:
				action = breakdown;
				break;
		}
	}
	
	switch (action) {

		case setup:
			if (db_setup())
				return EXIT_SUCCESS;
			else
				return EXIT_FAILURE;

		case create:
			if (!sel.link) {
				fputs ("You must specify URL with -u or --url option.\n", stderr);
				return EXIT_FAILURE;
			}
			if (db_create (&sel))
				return EXIT_SUCCESS;
			else
				return EXIT_FAILURE;

		case info:
			return db_info (&sel);

		case alter:
			return db_alter (&sel);

		case drop:
			return db_drop (&sel);

		case reader:
			return rssth_read (&sel, 0);

		case dump:
			return rssth_read (&sel, 1);

		case links:
			return print_links (&sel);

		case mark:
			return set_marks (&sel);

		case help:
			printf (RSSTH_HELP, progname);
			return EXIT_SUCCESS;

		case breakdown:
			if (optind < argc) {
				fprintf (stderr, "%s: incorrect option: %s\n",
						progname, argv[optind]);
			} else {
				fprintf (stderr, "%s: incorrect option\n", progname);
			}
			return EXIT_FAILURE;
	}
}
