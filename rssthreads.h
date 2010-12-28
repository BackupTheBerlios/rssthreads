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

#include <pthread.h>
#include <expat.h>
#include <argz.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <limits.h>
#include <libpq-fe.h>
#include <unistd.h> 
#include <time.h>
#include <errno.h>
#include <stdarg.h>
#include <fcntl.h> 
#include <locale.h>

#define RSSTH_DEFAULT_INTERVAL "1800"

typedef struct _rssItem {
	struct _rssContext *context; 	/* link to the item's context object */
	char *title;						/* item's title parsed */
	char *description;				/* item's description parsed */
	char *link;							/* item's link parsed */
	char *pubDate;						/* item's publication date parsed */
	char *guid;							/* item's GUID */
	char *cats;							/* argz vector of item's categories parsed */
	size_t cats_len;					/* lenght of the above vector */
	char *extra;						/* the place for any extra elements parsed */
	unsigned char append_flag;		/* some shit */
	unsigned char error;				/* error flag */
} *rss_item;
#define RSS_ITEM_SIZE sizeof(struct _rssItem)

typedef struct _rssContext /* rss_context type definition */ {
	char *tagName;					/* tag name */
	struct _rssContext *prev;	/* link to previous stack element */
	XML_Parser parser;			/* current parser */
	PGconn *db;						/* database connection object */
	time_t lastRecDate;			/* time of previous RSS capturing */
	struct selector *sel;		/* selector structure (defined below) */
	rss_item item;					/* item object (NULL when outside an item tag) */
	int pos;							/* position in the XML stack */
} *rss_context;
#define RSS_CONTEXT_SIZE sizeof(struct _rssContext)

/* rssthreads.c */ rss_context rsstopg_create_context (rss_context prev); /*
	creates rss_context object from object prev or just creates it
	when prev is null.  */
/* rssthreads.c */ void rsstopg_destroy_context (rss_context context); /*
	destroys rss_context object */

struct selector /* multi-purpose selection structure */ {
	char *dbKeys;						/* parameter to PQconnectdb() */
	char *id, *id2;
	unsigned short id2_auto;		/* whether id2 is auto-set from id */
	char *title;
	char *description;
	char *link, *link2;
	unsigned short link2_auto;		/* like with id */
	char *table, *table2;
	unsigned short table2_auto;  /* like with id */
	enum {qlf_none, qlf_id, qlf_table, qlf_url} qualifier;
										/* says what to use to determine RSS channel */
	char *active;
	char *interval;
	char *pubDate;
	char *recDate;
	unsigned short before;			/* before given date rather than from it */
	char *cats;
	//size_t catsLen;					/* unused */
	char *noCats;
	//size_t noCatsLen;					/* unused */
	short readMark, primaryMark, secondaryMark, deleteMark;
	char *sortField;
	unsigned short sortForward;	/* default is descending sort order */
	char *tuple;
	char *markSet;						/* argument of the '--mark' action */
	unsigned short hideExtra;	   /* default is descending sort order */
	char *browser;
};

/* parser.c */ void set_parser_callbacks(XML_Parser parser); /*
	installs XML parser callbacks */
/* parser.c */ int record_item (const rss_item item); /*
	makes database record from rss_item object.
	Returns false on failure */

/* http.c */ int http_open (const char *URL); /*
	creates TCP socket connection and starts HTTP session.
	Returns socket descriptor or zero on error. */

#define msg_verbose(...) msg_echo ("VERBOSE", __VA_ARGS__)
#define msg_debug(...) msg_echo ("DEBUG", __VA_ARGS__)
/* util.c */ void msg_echo (const char *, ...); /*
	outputs a message concatenating multiple strings
	followed by NULL pointer */
/* util.c */ char * concat (char *buf, ...); /*
	concatenates multiple strings, followed by NULL pointer into one
	in buffer buf, and returns buf */
/* util.c */ int isnumstr (const char *str); /*
	returns true if string contains only digits, and false otherwise */
/* util.c */ int isncstr (const char *str); /*
	returns false if string contains something but digits or commas */
/* util.c */ char* termstr (const char *mem, int len); /*
	appends zero-terminator to unterminated string mem of length len */
/* util.c */ int makestring (char **str, const char *mem, int len); /*
	if *str is NULL, creates string from mem of length len;
   otherwise, appends mem to *str */
/* util.c */ time_t parse_time (const char *timestr); /*
	parses time in RFC822 format and returns time_t */
/* util.c */ char * argz_last (const char *argz, size_t argz_len); /*
	returns last entry of argz vector */
/* util.c */ char * append (char **buf, const char *str); /*
	appends string str to string *buf with memory reallocation to conform to
	new string length. When *buf is NULL just allocates memory and copies
	str to it. Places address of new string to *buf and returns it. */
/* util.c */ int get_screen_columns (void); /* 
	returns screen width */
/* util.c */ char * word_wrap (char **str); /*
	makes word wrap on string str according to screen width and returns str */
/* util.c */ int append_where_clause (char **sql, struct selector *sel, const char *table); /*
	constructs a SQL WHERE-clause from selector _sel_ and appends it to the
	string pointed by *sql using append() function (returns new pointer in
	**sql); returns 1 if something was made and 0 otherwise (in the case when
	all the appropriate selector's fields are null). The third argument is
	the table name.
	*/
/* util.c */ int append_from_categories (char **sql, struct selector *sel, const char* table); /*
	appends a Category and CategoryIndex tables names to FROM clause (and
	returns 1) when needed. Returns 0 otherwise. Argument meanings is the same as
	in append_where_clause() */
/* util.c */ void append_orderby_clause (char **sql,  struct selector *sel); /*
	appends ORDER BY clause to sql string using selector sel */
/* util.c */ int obtain_table_name (char *table, PGconn *db, struct selector *sel); /*
	Obtains table name from sel->table or from database, using sel->link or
	sel->id, and outputs it into **table. Returns 1 on success. Closes 
	connection db and returns 0 on failure. */
#if 0 /* crude and should not be used */
/* util.c */ int obtain_url (char **url, PGconn *db, struct selector *sel); /*
	Obtains table url from sel->link or from database, using sel->table or
	sel->id, and outputs it's pointer into **url. Output must be freed after use.
	Returns 1 on success. Closes connection db and returns 0 on failure. */
#endif
/* util.c */ int select_tuple (char *table, struct selector *sel, PGconn* db, char *retbuf); /*
	Obtains ID of single item from the sel-corresponding query result using
	the sel->tuple field and places that ID into retbuf (in the string
	representation). */
/* util.c */ void draw_vline(void); /*
	draw screen-wide vertical line */
#ifndef _GNU_SOURCE
/* util.c */ void * mempcpy (void *dest, const void *src, size_t n); /*
	replacement for the library function that depends on the _GNU_SOURCE macro */
#endif
/* util.c */ int translate_encoding (char *charset); /*
	Attempt to translate RSS encoding designation go PG one. Return true
	on success.
*/

/* db.c */ PGconn * db_connect (const struct selector *sel); /*
	establishes database connection */
/* db.c */ PGresult * db_exec ( PGconn *db, const char *sql, unsigned int nParms, ...); /*
	convenience wrapper for PQexec() and PQexecParams().

	When there is no parameters (nParms = 0), PQexec (db, sql) is called.
	In other case PQexecParams() is called this way:

	PQexecParams (db, sql, nParms, NULL, <array from optional arguments>,
			NULL, NULL, 0);
	
	Frees PGresult in cases of failure or no-data.  */
/* db.c */ int db_setup (void); /*
	initializes new database. Returns boolean. */
/* db.c */ int db_create (const struct selector *sel); /*
	registers new feed and creates table set for it. */
/* db.c */ int db_info (struct selector *sel); /*
	prints info on some channel or lists channels
	(when sel->qualifier is none) */
/* db.c */ int db_drop (struct selector *sel); /*
	deletes some feed with it's tables from database. */
/* db.c */ int db_alter (struct selector *sel); /*
	modify parameters (url, description, active/non-active) of some channel */
/* db.c */ PGconn * db_open (rss_context context, const struct selector *sel); /*
	opens db connection for given url, determines table name
	and prepares statements. Returns NULL on failure. */
/* db.c */ void db_close (PGconn *db); /*
	closes database connection db */

/* tools.c */ int rsstopg_read (struct selector *sel, unsigned short dump); /*
	main rss reader/dumper function */
/* tools.c */ int print_links (struct selector *sel); /*
	prints list of links of the items, that corespond to sel */
/* tools.c */ int set_marks (struct selector *sel); /*
	set marks on item(s) */
