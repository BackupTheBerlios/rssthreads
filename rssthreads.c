/*
    Copyright (C) 2010  Serge V. Baumer

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

rss_context rssth_create_context (rss_context prev) {
	rss_context context = (rss_context) calloc (1, RSS_CONTEXT_SIZE);
	if (prev) {
		context->db = prev->db;
		context->sel = prev->sel;
		context->parser = prev->parser;
		context->lastRecDate = prev->lastRecDate;
		context->prev = prev;
		context->pos = prev->pos + 1;
	} else {
		context->pos = -1;
	}
}

void rssth_destroy_context (rss_context context) {
	free (context->tagName);
	free (context);
}

int rssth_get (rss_context context) {
	char *url = context->sel->link;
	PGconn *db = context->db;
	PGresult *res;

	/* open channel for reading XML data */
	ssize_t s;
	char buf[BUFSIZ];
	int infile;
	if (!strncmp(url, "file://", 7)) {
		if ((infile = open(url+7, O_RDONLY)) == -1) {
			msg_echo ("Couldn't open", url, NULL);
			return 0;
		}
	} else {
		if (!(infile = http_open(url))) {
			msg_echo ("Couldn't open", url, NULL);
			return 0;
		}
	}

	res = db_exec (db, concat(buf, 
				"SELECT RecDate FROM ", context->sel->table,
				" ORDER BY RecDate DESC LIMIT 1", NULL), 0);
	if (PQntuples (res)) {
		struct tm _lastRecDate, *lastRecDate = &_lastRecDate;
		memset (lastRecDate, 0, sizeof(struct tm));
		strptime (PQgetvalue(res, 0, 0), "%Y-%m-%d %H:%M:%S%z", lastRecDate);
		time_t offset = lastRecDate->tm_gmtoff;
		lastRecDate->tm_gmtoff = 0;
		context->lastRecDate = timegm (lastRecDate) - offset;
	} else {
		context->lastRecDate = 0;
	}
	PQclear (res);
	
	XML_SetUserData (context->parser, context);

	/* parsing loop */
	while (s = read (infile, buf, BUFSIZ)) {
		if (s == -1) {
			msg_echo ("Read error with", url, NULL);
			return 0;
		}
		if (! XML_Parse (context->parser, buf, s, 0))
			msg_echo ("XML Parse failure:",
					XML_ErrorString(XML_GetErrorCode(context->parser)), NULL);
	}

	if (! XML_Parse (context->parser, buf, 0, 1))
		msg_echo ("XML Parse failure:",
				XML_ErrorString(XML_GetErrorCode(context->parser)), NULL);

	close (infile);
}

int rss_thread (void *arg) {
	struct selector *sel = (struct selector *) arg;
	unsigned int interval;
	XML_Parser parser;

	for (interval = strtoul(sel->interval, NULL, 0); ; sleep(interval)) {
		pthread_setcancelstate (PTHREAD_CANCEL_DISABLE, NULL);

		msg_verbose ("begin working with", sel->link, NULL);
		/* create XML parser and set up its callbacks */
		if ( ! (parser = XML_ParserCreate (NULL))) 
			msg_echo ("Cannot create parser", NULL);
		set_parser_callbacks(parser);

		/* create initial parsing context (stack base) */
		rss_context context = rssth_create_context (NULL);
		
		context->parser = parser;
		context->sel = sel;

		PGconn *db;
		PGresult *res;
		db = db_open (context, sel);

		rssth_get (context);

		/* cleanup */
		db_close (db);
		XML_ParserFree (parser);
		rssth_destroy_context (context);

		msg_verbose ("end working with", sel->link,
				"\n\tinterval:", sel->interval, "seconds.", NULL);
		pthread_setcancelstate (PTHREAD_CANCEL_ENABLE, NULL);
		pthread_testcancel();
	}
	return 1;
}

pthread_t *threadsptr;
int numthreads;

void interrupt_handler (int sig) {
	pthread_t *thread = threadsptr;
	int i;
	char *signame;
	char buf[15];

	switch (sig) {
		case 1:
			signame = "SIGHUP"; break;
		case 2:
			signame = "SIGINT"; break;
		case 15:
			signame = "SIGTERM"; break;
		default:
			signame = buf; 
			sprintf (buf, "signal %d", sig);
	}
	msg_verbose ("interrupted by", signame, NULL);

	for (i = 0; i < numthreads; i++, thread++) {
		pthread_cancel (*thread);
	}	
}

int rssth_collect (struct selector *sel) {
	PGconn *db = db_connect(sel);
	if (!db) return EXIT_FAILURE;
	PGresult *res = db_exec (db, "SELECT "
			"ID, URL, TableName, EXTRACT (epoch FROM Interval) "
			"FROM RSS WHERE Active IS true", 0);
	db_close (db);
	if (!res) return EXIT_FAILURE;

	int ntuples = PQntuples (res);
	if (ntuples) {
		struct selector th_sel[ntuples];
		pthread_t thread[ntuples];

		numthreads = ntuples;
		threadsptr = thread;

		struct sigaction act, oact;
		sigset_t set, oset;
		sigprocmask (SIG_SETMASK, NULL, &set);
		sigaddset (&set, SIGINT);
		sigaddset (&set, SIGTERM);
		sigaddset (&set, SIGHUP);
		act.sa_mask = set;
		act.sa_flags = 0;
		act.sa_handler = interrupt_handler;
		sigaction (SIGINT, &act, NULL);
		sigaction (SIGTERM, &act, NULL);
		sigaction (SIGHUP, &act, NULL);

		pthread_sigmask (SIG_SETMASK, &set, &oset);

		int i;
		for (i = 0; i < ntuples; i++) {
			th_sel[i] = *sel;
			th_sel[i].id = PQgetvalue(res, i, 0);
			th_sel[i].link = PQgetvalue(res, i, 1);
			th_sel[i].table = PQgetvalue(res, i, 2);
			th_sel[i].interval = PQgetvalue(res, i, 3);

			pthread_create (&thread[i], NULL,
					(void *) &rss_thread, (void *) &th_sel[i]);
		}

		pthread_sigmask (SIG_SETMASK, &oset, NULL);

		for (i = 0; i < ntuples; i++) {
			pthread_join (thread[i], NULL);
		}
	}
	PQclear (res);
	return EXIT_SUCCESS;
}

const char *progname;

#define LONGOPTS_INDEX \
	"a::bc::d::e:f:g:hi::l:m:n:op:r::st:u:v:x:" \
	"A:B:F:G:I:K:L::M:OP::R:T:U:X"
static const struct option longopts[] = {
	{"setup", no_argument, NULL, 's'},
	{"create", optional_argument, NULL, 'c'},
	{"info", optional_argument, NULL, 'i'},
	{"alter", optional_argument, NULL, 'a'},
	{"drop", optional_argument, NULL, 'd'},
	{"read", optional_argument, NULL, 'r'},
	{"dump", optional_argument, NULL, 'P'},
	{"print-links", optional_argument, NULL, 'L'},
	{"mark", required_argument, NULL, 'F'},
	/* end of actions */
	{"db-keys", required_argument, NULL, 'K'},
	{"active", required_argument, NULL, 'A'},
	{"url", required_argument, NULL, 'u'},
	{"table", required_argument, NULL, 'p'},
	{"interval", required_argument, NULL, 'n'},
	{"id", required_argument, NULL, 'I'},
	{"title", required_argument, NULL, 't'},
	{"link", required_argument, NULL, 'l'},
	{"description", required_argument, NULL, 'T'},
	{"pubdate", required_argument, NULL, 'U'},
	{"recdate", required_argument, NULL, 'R'},
	{"before", no_argument, NULL, 'b'},
	{"categories", required_argument, NULL, 'g'},
	{"no-categories", required_argument, NULL, 'G'},
	{"extra", no_argument, NULL, 'x'},
	{"no-extra", no_argument, NULL, 'X'},
	{"marked", required_argument, NULL, 'm'},
	{"not-marked", required_argument, NULL, 'M'},
	{"sort-field", required_argument, NULL, 'f'},
	{"sort-asc", no_argument, NULL, 'o'},
	{"sort-desc", no_argument, NULL, 'O'},
	{"tuple", required_argument, NULL, 'e'},
	{"browser", required_argument, NULL, 'B'},
	{"verbose", required_argument, NULL, 'v'},
	{"help", no_argument, NULL, 'h'},
	{NULL, 0, NULL, 0}
};

typedef enum {
	collect, setup, create, info, alter, drop, reader,
	dump, links, mark, breakdown, help
} action_var;

unsigned short rssth_verbose = 0;

int main (int argc, char **argv) {
	action_var action = collect;
	int opt;
	progname = argv[0];

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
	#define set_mark(VALUE) switch (*optarg) { \
		case 'r': sel.readMark = VALUE; break; \
		case 'p': sel.primaryMark = VALUE; break; \
		case 's': sel.secondaryMark = VALUE; break; \
		case 'd': sel.deleteMark = VALUE; break; \
	}

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
			case 'u':
				set_link(); break;
			case 'p':
				set_table() break;
			case 'n':
				sel.interval = optarg; break;
			case 'A':
				sel.active = optarg; break;
			case 'I':
				set_id(); break;
			case 't':
				sel.title = optarg; break;
			case 'l':
				set_link(); break;
			case 'U':
				sel.pubDate = optarg; break;
			case 'R':
				sel.recDate = optarg; break;
			case 'b':
				sel.before = 1; break;
			case 'T':
				sel.description = optarg; break;
			case 'g':
				sel.cats = optarg; break;
			case 'G':
				sel.noCats = optarg; break;
			case 'x':
				sel.hideExtra = 0; break;
			case 'X':
				sel.hideExtra = 1; break;
			case 'm':
				set_mark (1); break;
			case 'M':
				set_mark (-1); break;
			case 'f':
				sel.sortField = optarg; break;
			case 'o':
				sel.sortForward = 1; break;
			case 'O':
				sel.sortForward = 0; break;
			case 'e':
				sel.tuple = optarg; break;
			case 'B':
				sel.browser = optarg; break;
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

		case collect:
			return rssth_collect (&sel);

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
