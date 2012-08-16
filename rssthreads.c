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
#include "th_help.h"

rss_context rssth_create_context (rss_context prev) {
	rss_context context = (rss_context) calloc (1, RSS_CONTEXT_SIZE);
	if (prev) {
		context->db = prev->db;
		context->sel = prev->sel;
		context->parser = prev->parser;
		context->lastRecDate = prev->lastRecDate;
		context->prev = prev;
		context->itemCounter = prev->itemCounter;
		context->pos = prev->pos + 1;
	} else {
		context->pos = -1;
	}
}

void rssth_destroy_context (rss_context context) {
	if (context->prev) {
		context->prev->itemCounter = context->itemCounter;
	}

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
		if (!(infile = http_open(context->sel))) {
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
		char* timeString = PQgetvalue(res, 0, 0);
		msg_debug ("Last recording time:", timeString, NULL);
		strptime (timeString, "%Y-%m-%d %H:%M:%S%z", lastRecDate);
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
	char buf[50];
	XML_Parser parser;

	for (interval = strtoul(sel->interval, NULL, 0); ; sleep(interval)) {
		pthread_setcancelstate (PTHREAD_CANCEL_DISABLE, NULL);

		msg_verbose (sel->table, ": transfer from", sel->link, "begin", NULL);
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
		if (PQsetClientEncoding(db, "UTF8")) {
			msg_echo ("PQsetClientEncoding() failed. Current encoding is",
					pg_encoding_to_char(PQclientEncoding(db)), NULL);
		}


		rssth_get (context);

		/* cleanup */
		db_close (db);
		XML_ParserFree (parser);

		if (context->itemCounter) {
			sprintf (buf, ": %u new items", context->itemCounter);
		} else {
			strcpy (buf, ": transfer end");
		}
		msg_verbose (sel->table, buf,
				"\n\tinterval:", sel->interval, "seconds.", NULL);

		rssth_destroy_context (context);
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
	char i_str[10];
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

	for (i = 0; i < numthreads; i++, thread++) {
		sprintf (i_str, "%u", i);
		msg_verbose ("cancelling thread", i_str, NULL);
		pthread_cancel (*thread);
		msg_verbose ("\tthread cancelled", NULL);
	}	

	msg_verbose ("interrupted by", signame, NULL);
}

int rssth_collect (struct selector *sel) {
	PGconn *db = db_connect(sel);
	PGresult *res;
	if (!db) return EXIT_FAILURE;
	res = db_exec (db, "SELECT "
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
		char i_str[10];
		for (i = 0; i < ntuples; i++) {
			sprintf (i_str, "%d", i);
			th_sel[i] = *sel;
			th_sel[i].id = PQgetvalue(res, i, 0);
			th_sel[i].link = PQgetvalue(res, i, 1);
			th_sel[i].table = PQgetvalue(res, i, 2);
			th_sel[i].interval = PQgetvalue(res, i, 3);

			sleep (15);
			msg_verbose ("starting thread for", th_sel[i].table, 
					"; id =", i_str, NULL);
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
	"hv:" \
	"K:"
static const struct option longopts[] = {
	{"db-keys", required_argument, NULL, 'K'},
	{"verbose", required_argument, NULL, 'v'},
	{"help", no_argument, NULL, 'h'},
	{NULL, 0, NULL, 0}
};

typedef enum {
	collect, help, breakdown
} action_var;

unsigned short rssth_verbose = 0;

int main (int argc, char **argv) {
	action_var action = collect;
	int opt;
	progname = argv[0];

	setlocale (LC_ALL, "");

	struct selector sel;
	memset (&sel, 0, sizeof(sel));
	sel.dbKeys = "";

	/* several macros for the options parsing loop */

	#define set_action(ACTION) if (action != breakdown) action = ACTION

	while ((opt = getopt_long (argc, argv, LONGOPTS_INDEX,
					longopts, NULL)) != -1) {
		switch (opt) {
			case 'K':
				sel.dbKeys = optarg; break;
			case 'v':
				rssth_verbose = strtoul (optarg, NULL, 0); break;
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
