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

extern unsigned short rssth_verbose;

void msg_echo (const char *begin, ...) {
	char *buf = NULL;
	const char *chunk;
	va_list chunks;
	va_start (chunks, begin);

	if (!strcmp(begin, "DEBUG") && (rssth_verbose < 2))
		return;
	else if (!strcmp(begin, "VERBOSE") && (rssth_verbose < 1))
		return;
	else if (strcmp(begin, "DEBUG") && strcmp(begin, "VERBOSE"))
		append (&buf, begin);

	if (!strcmp(begin, "VERBOSE") || !strcmp(begin, "DEBUG")) {
		chunk = va_arg (chunks, char*);
		append (&buf, chunk);
	}

	while (chunk = va_arg (chunks, char*)) {
		append (&buf, " ");
		append (&buf, chunk);
	}
	append (&buf, "\n");

	fputs (buf, stderr);

	free (buf);
}

char * concat (char *buf, ...) {
	const char *chunk;
	va_list chunks;
	va_start (chunks, buf);
	strcpy (buf, va_arg (chunks, char*));
	while (chunk = va_arg (chunks, char*))
		strcat (buf, chunk);
	return buf;
}

#if 0 // not used
int isnumstr (const char *str) {
	char *ptr = str;
	while (*ptr != '\0' && *ptr !='\n') {
		if (!isdigit (*(ptr++)))
			return 0;
	}
	return 1;
}
#endif

int isncstr (const char *str) {
	char *ptr = str;
	while (*ptr != '\0' && *ptr !='\n') {
		if (!isdigit (*ptr) && *ptr != ',')
			return 0;
		ptr++;
	}
	return 1;
}

#if 0 //not used
char* termstr (const char *mem, int len) {
	char *str = malloc (len + 1);
	str = (char*) memcpy(str, mem, len);
	*(str + len) = '\0';
	return str;
}
#endif

char * append (char **buf, const char *str) {
	char *newbuf;

	if (*buf) {
		newbuf = malloc (strlen(*buf) + strlen(str) + 1);
		strcpy (newbuf, *buf);
		strcat (newbuf, str);
		free (*buf);
	} else {
		newbuf = malloc (strlen(str) + 1);
		strcpy (newbuf, str);
	}

	*buf = newbuf;
	return *buf;
}

int obtain_table_name (char *table, PGconn *db, struct selector *sel) {
	PGresult *res = NULL;

	switch (sel->qualifier) {
		case qlf_table:
			strcpy (table, sel->table);
			return 1;
		case qlf_id:
			if (sel->id2_auto) sel->id2 = NULL;
			res = db_exec (db, "SELECT TableName FROM RSS WHERE ID = $1",
					1, sel->id);
			break;
		case qlf_url:
			if (sel->link2_auto) sel->link2 = NULL;
			res = db_exec (db, "SELECT TableName FROM RSS WHERE URL = $1",
					1, sel->link);
			break;
		case qlf_none:
			msg_echo ("Neither table name, nor URL, nor ID was given.", NULL);
			db_close (db);
			return 0;
	}

	if (!res) {
		db_close (db);
		return 0;
	}
	
	if (!PQntuples(res)) {
		msg_echo ("Such feed was not found.", NULL);
		PQclear (res);
		db_close (db);
		return 0;
	}

	strcpy (table, PQgetvalue(res, 0, 0));
	PQclear (res);

	return 1;
}

#if 0 /* crude so should not be used */
int obtain_url (char **url, PGconn *db, struct selector *sel) {
	PGresult *res = NULL;

	if (!sel->link) {
		if (sel->table) {
			res = db_exec (db, "SELECT URL FROM RSS WHERE TableName = $1",
					1, sel->table);
		} else if (sel->id) {
			if (sel->id2_auto) sel->id2 = NULL;
			res = db_exec (db, "SELECT URL FROM RSS WHERE ID = $1",
					1, sel->id);
		} else {
			msg_echo ("Neither table name, nor URL, nor ID was given.", NULL);
			db_close (db);
			return 0;
		}

		if (!res) {
			db_close (db);
			return 0;
		}
		
		if (!PQntuples(res)) {
			msg_echo ("Such feed was not found.", NULL);
			PQclear (res);
			db_close (db);
			return 0;
		}

		*url = strdup (PQgetvalue(res, 0, 0));
		PQclear (res);
	} else {
		if (sel->link2_auto) sel->link2 = NULL;
		*url = strdup (sel->link);
	}

	return 1;
}
#endif

#ifndef _GNU_SOURCE
void * mempcpy (void *dest, const void *src, size_t n) {
	memcpy (dest, src, n);
	char *retval = (char *) dest + n;
	return (void *) retval;
} 
#endif

#if 0 //not used
int translate_encoding (char *encoding) {
	#define ENCQ (sizeof(encname) / sizeof(encname[0]))
	unsigned int i;
	int translated = 0;

	msg_debug ("name translation for encoding:", encoding, NULL);

	for (i = 0 ; i < ENCQ ; i++) {
		if (!strcmp (encoding, encname[i].rss)) {
			translated = 1;
			encoding = encname[i].pg;
			break;
		}
	}

	msg_debug ("\tresulting encoding name:", encoding, NULL);

	return translated;
}
#endif

#ifdef RSS_TH

int makestring (char **str, const char *mem, int len) {
	char nbuf[20];
	if (*str) {
		//printf ("str: %s, mem: %s, len: %d", *str, mem, len);
		char *newstr = (char *) realloc ((void *)*str,
				strlen(*str) + (size_t)len + (size_t)1);
		if (!newstr) {
			sscanf(nbuf, "%i", len);
			msg_echo ("realloc() failed in makestring()\n",
					"\t*str:", *str, "\n\tlen:", nbuf, NULL);
			free (*str);
			return 0;
		} 
		*str = strncat (newstr, mem, len);
	} else {
		*str = malloc (len + 1);
		if (! *str) {
			sscanf(nbuf, "%i", len);
			msg_echo ("malloc() failed in makestring()\n",
					"\tlen:", nbuf, NULL);
			return 0;
		}
		memcpy ((void *)*str, (void *)mem, (size_t)len);
		*(*str + len) = '\0';
	}
	return 1;
}

time_t parse_time (const char *timestr) {
	setlocale (LC_ALL, "C");

	time_t base;
	long int offset = 0;
	struct tm brtime;
	char *tz, *ptr;
	char buf[LINE_MAX];

	strcpy (buf, timestr);
	if (!(tz = strptime (buf, "%a, %d %b %Y %H:%M:%S", &brtime)))
		tz = strptime (buf, "%d %b %Y %H:%M:%S", &brtime);
	base = timegm (&brtime);

	while (*tz == ' ') tz++;
	ptr = tz + strlen (tz) - 1;
	while (*ptr == ' ') ptr--;
	*(ptr+1) = '\0';

	if (*tz == '+' || *tz == '-') {
		if (strlen(tz) == 5) {
			ptr = tz + 3;
			offset = atoi(ptr) * 60;
			*ptr = '\0';
		}
		ptr = tz + 1;
		offset += atoi(ptr) * 3600;
		if (*tz == '-') offset = 0 - offset;
	} else {
		if (!strcmp (tz, "UT") || !strcmp(tz, "GMT")) offset = 0;
		else if (!strcmp(tz, "EST") || !strcmp(tz, "CDT")) offset = -5;
		else if (!strcmp(tz, "CST") || !strcmp(tz, "MDT")) offset = -6;
		else if (!strcmp(tz, "MST") || !strcmp(tz, "PDT")) offset = -7;
		else if (!strcmp(tz, "EDT")) offset = -4;
		else if (!strcmp(tz, "PST")) offset = -8;
		else {
			msg_echo ("Unknown timezone:", tz, NULL);
			return 0;
		}
		offset *= 3600;
	}
	time_t result = base - offset;
//printf ("Time: %s", ctime (&result));
	setlocale (LC_ALL, "");
	return base - offset;
}

char * argz_last (const char *argz, size_t argz_len) {
	char *entry = NULL, *prev = NULL;
	while (entry = argz_next (argz, argz_len, entry))
		prev = entry;
	return prev;
}

#endif

#ifdef RSS_PG

int get_screen_columns(void) {
	char *cols;
	int ncols;
	if (!(cols = getenv("COLUMNS")))
		ncols = 80;
	else 
		ncols = strtol (cols, NULL, 0);
	return ncols;
}

char * word_wrap (char **str) {
	int cols = get_screen_columns();
	char *begin = *str;
	char *end = begin + strlen(begin);
	char *cursor, *last_space = NULL, *last_linebreak = NULL;
	int col_counter;

	for (cursor=begin, col_counter=1 ; cursor<=end ; cursor++, col_counter++) {
		if (*cursor == '\n') {
			last_space = last_linebreak = cursor;
			col_counter = 0;
			continue;
		}

		if (isspace (*cursor))
			last_space = cursor;

		if ((col_counter==cols) && (last_space>last_linebreak)) {
				*last_space = '\n';
				cursor = last_linebreak = last_space;
				col_counter = 0;
		}
	}

	return *str;
}

int pipe_output (const char *text, const char *cmd) {
	FILE *descf;
	char *shcmd = NULL;
	append (&shcmd,"sh -c '");
	append (&shcmd, cmd);
	append (&shcmd, "'");
	fflush (NULL);
	descf = popen (shcmd, "w");
	free (shcmd);
	if (!descf) {
		msg_echo ("can't execute filter command:", cmd, NULL);
		return 0;
	}
	fprintf (descf, "%s\n", text);
	pclose (descf);
	return 1;
}
	
int append_where_clause (char **sql, struct selector *sel, const char* table) {
	int retval = 0;
	char buf[LINE_MAX];
	//char tableName[38];
	//strcat (strcpy(tableName, prefix), "_Feed");

	if (sel->id2 || sel->link2 || sel->pubDate || sel->recDate || sel->cats ||
			sel->noCats || sel->readMark || sel->primaryMark ||
			sel->secondaryMark || sel->deleteMark || sel->title || sel->title_cs ||
			sel->title_nomatch || sel->title_nomatch_cs || sel->description ||
			sel->description_cs || sel->description_nomatch || 
			sel->description_nomatch_cs ) {
		retval = 1;
		append (sql, " WHERE ");

		char *token, *junction;
		unsigned short conds = 0;  /* zero if it's no previous WHERE conditions */
		if (sel->disjunction)
			junction = " OR ";
		else
			junction = " AND ";

		#define conditional_and() \
			if (conds) append (sql, junction); else conds = 1;
		
		if (sel->id2) {
			conds = 1;
			append_list (sql, sel->id2, concat(buf, table, ".ID", NULL));
		}
		
		if (sel->title) {
			conditional_and();
			append (sql, concat(buf, table, ".Title ~* E'",
						sel->title, "'", NULL));
		}

		if (sel->title_cs) {
			conditional_and();
			append (sql, concat(buf, table, ".Title ~ E'",
						sel->title_cs, "'", NULL));
		}

		if (sel->title_nomatch) {
			conditional_and();
			append (sql, concat(buf, table, ".Title !~* E'",
						sel->title_nomatch, "'", NULL));
		}

		if (sel->title_nomatch_cs) {
			conditional_and();
			append (sql, concat(buf, table, ".Title !~ E'",
						sel->title_nomatch_cs, "'", NULL));
		}

		if (sel->description) {
			conditional_and();
			append (sql, concat(buf, table, ".Description ~* E'",
						sel->description, "'", NULL));
		}

		if (sel->description_cs) {
			conditional_and();
			append (sql, concat(buf, table, ".Description ~ E'",
						sel->description_cs, "'", NULL));
		}

		if (sel->description_nomatch) {
			conditional_and();
			append (sql, concat(buf, table, ".Description !~* E'",
						sel->description_nomatch, "'", NULL));
		}

		if (sel->description_nomatch_cs) {
			conditional_and();
			append (sql, concat(buf, table, ".Description !~ E'",
						sel->description_nomatch_cs, "'", NULL));
		}

		if (sel->link2) {
			conditional_and();
			append_list (sql, sel->link2, concat(buf, table, ".Link", NULL));
		}

		if (sel->before) token="<"; else token = ">=";
		if (sel->pubDate) {
			conditional_and();
			append (sql, concat(buf, table, ".PubDate", token,
						"'", sel->pubDate, "'", NULL));
		}
		if (sel->recDate) {
			conditional_and();
			append (sql, concat(buf, table, ".RecDate", token,
						"'", sel->recDate, "'", NULL));
		}

		if (sel->cats || sel->noCats) {
			conditional_and();
			append (sql, concat(buf,
						table, ".ID=", table, "_Categories.Item", NULL));
		}
		if (sel->cats) {
			append (sql, junction); 
			append_list (sql, sel->cats, concat(buf,
						table, "_Categories.Category", NULL));
		}
		if (sel->noCats) {
			char *saveptr;
			append (sql, junction);
			token = strtok_r(sel->noCats, ",", &saveptr);
			append (sql, concat(buf, "(", table, ".ID", 
						" NOT IN (SELECT ", table, "_Categories.Item FROM ",
						table, "_Categories WHERE ",
						table, "_Categories.Category=", "'", token, "')", NULL));
			while (token = strtok_r(NULL, ",", &saveptr))
				append (sql, concat(buf, junction, table, ".ID", 
						" NOT IN (SELECT ", table, "_Categories.Item FROM ",
						table, "_Categories WHERE ",
						table, "_Categories.Category=", "'", token, "')", NULL));
			append (sql, ")");
		}

		if (sel->readMark) {
			conditional_and();
			append_mark (sql, sel->readMark, 
					concat(buf, table, ".ReadMark", NULL));
		}
		if (sel->primaryMark) {
			conditional_and();
			append_mark (sql, sel->primaryMark, 
					concat(buf, table, ".PrimaryMark", NULL));
		}
		if (sel->secondaryMark) {
			conditional_and();
			append_mark (sql, sel->secondaryMark, 
					concat(buf, table, ".SecondaryMark", NULL));
		}
		if (sel->deleteMark) {
			conditional_and();
			append_mark (sql, sel->deleteMark, 
					concat(buf, table, ".DeleteMark", NULL));
		}
	}	
	return retval;
}

int append_from_categories (char **sql, struct selector *sel,
		const char* table) {
	char buf[100];
	if (sel->cats || sel->noCats) {
		append (sql, concat (buf, ",", 
					table, "_Categories", NULL));
		return 1;
	}
	return 0;
}
	
void append_orderby_clause (char **sql,  struct selector *sel) {
	char *sortOrder;
	char buf[100];

	if (!sel->sortField) sel->sortField = "PubDate";
	if (!sel->sortForward)
		sortOrder = " DESC";
	else
		sortOrder = "";

	append (sql, concat(buf, " ORDER BY ", sel->sortField, sortOrder, NULL));
}

int select_tuple (char *table, struct selector *sel, PGconn* db, char *retbuf)
{
	PGresult *res;
	char *sql = NULL;
	char buf[LINE_MAX];

	append (&sql, concat(buf, "SELECT DISTINCT ",
	  			table, ".ID,",
				table, ".", (sel->sortField ? sel->sortField : "PubDate"),
				" FROM ",  table, NULL));
	append_from_categories (&sql, sel, table);
	append_where_clause (&sql, sel, table);
	append_orderby_clause (&sql, sel);
	
	unsigned int tuple, ztuple, ntuples;
	tuple = strtoul (sel->tuple, NULL, 0);
	ztuple = tuple - 1; /* in PGresult the enumeration begins from zero */
	
	if (!(res = db_exec (db, sql, 0))) {
		free (sql);
		db_close (db);
		return 0;
	}

	ntuples = PQntuples (res);
	if (tuple > ntuples) {
		sscanf (buf, "%u", ntuples);
		msg_echo ("The selection returns only", buf, "tuples.", NULL);
		return 0;
	}

	strcpy (retbuf, PQgetvalue (res, ztuple, 0));
	PQclear (res);
	return 1;
}

void draw_vline(void) {
	int n;
	for (n = 0; n < get_screen_columns() ; n++)
		putchar ('=');
	putchar ('\n');
}


#endif
