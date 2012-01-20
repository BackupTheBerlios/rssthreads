/*
    Copyright (C) 2010-2012  Serge V. Baumer

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
#include "encodings.h"

void XMLCALL rss_xml (void *data, const XML_Char *version,
		const XML_Char *encoding, int standalone) {
	rss_context context = (rss_context) data;
	
#if 0
	if (encoding) {
		char _enc[30], *enc = _enc;
		strcpy (enc, encoding);
		translate_encoding (enc);

		if (PQsetClientEncoding(context->db, enc) == -1) 
			msg_verbose ("PQsetClientEncoding() for encoding",
					enc, "failed.", NULL);
	}
#endif
}

void XMLCALL rss_opentag(void *data, const char *element, const char **attributes) {
	rss_context context, nextContext;
	context = (rss_context) data;
	XML_Parser parser = context->parser;
	
	/* create new context */
	nextContext = rssth_create_context (context);
	nextContext->tagName = strdup (element);
	/* create item object for new "item" element */
	if (!strcmp (element, "item")) {
		nextContext->item = (rss_item) calloc (1, RSS_ITEM_SIZE);
		nextContext->item->context = nextContext;
	} else
		nextContext->item = context->item;
	/* put new context on stack */

	XML_SetUserData (parser, nextContext);
}

int XMLCALL rss_unknown_enc (void *data, const char *name,
		XML_Encoding *info) {
	#define ENCMAPS_LENGTH (sizeof(encmaps)/XML_ENCODING_MAP_SIZE)
	int i, code = 0;
	for (i = 0 ; i < ENCMAPS_LENGTH ; i++) {
		if (!strcmp (name, encmaps[i].name)) {
			for ( ; code < 256 ; code++) {
				info->map[code] = encmaps[i].map[code];
			}
		}
	}
	if (!code) {
		return XML_STATUS_ERROR;
	} else {
		info->data = NULL;
		info->convert = NULL;
		info->release = NULL;
		return XML_STATUS_OK;
	}
}

void XMLCALL rss_cdata(void *data, const char *str, int len) {
	//puts ("rss_cdata");
	rss_context context;
	char buf[LINE_MAX];

	context = (rss_context) data;
	//	fflush (stdout); fputs ("<<<CHAR DATA>>>\n", stderr); fflush(stderr);

	/* when inside "item", fill appropriate item object fields */
	if (context->item) {
		rss_item item = context->item;
		char *tstr;

		#define make_field(FLD) if (!makestring (&item->FLD, str, len)) { \
			item->error = 1; \
			return; \
		}

		if (!strcmp (context->tagName, "title")) {
			make_field (title);
		} else if (!strcmp (context->tagName, "link")) {
			make_field (link);
		} else if (!strcmp (context->tagName, "description")) {
			make_field (description);
		} else if (!strcmp (context->tagName, "pubDate")) {
			make_field (pubDate);
		} else if (!strcmp (context->tagName, "guid")) {
			make_field (guid);
		} else if (!strcmp (context->tagName, "category")) {
			if (item->append_flag) {
				char *oldstring = argz_last (item->cats, item->cats_len);
				char *tmpstring = (char *) malloc (strlen (oldstring)+1);
				if (!tmpstring) {
					msg_echo ("malloc() failed in rss_cdata() on category parsing",
							NULL);
					item->error = 1;
					return;
				}
				strcpy (tmpstring, oldstring);
				if (!makestring (&tmpstring, str, len)) {
					item->error = 1;
					return;
				}
			//	printf ("argz_replace: %s", item->cats);
				if (argz_replace (&item->cats, &item->cats_len,
						oldstring, tmpstring, NULL) == ENOMEM)
					puts ("argz ENOMEM");
				free (tmpstring);
			} else {
				if (!(tstr = (char *)malloc (len + 1))) {
					msg_echo ("malloc() (2) failed in rss_cdata() on category "
							"parsing", NULL);
					return;
				}
				*((char *)(mempcpy (tstr, str, len))) = '\0';
				//puts (item->title);
				//puts (tstr);
				//printf ("argz_append: %s cats_len: %d len: %d tstr: %s tstr len: %d\n", item->cats, item->cats_len, len, tstr, strlen (tstr));
				if (argz_add (&(item->cats), &(item->cats_len), tstr) == ENOMEM)
					puts ("argz ENOMEM");
				//puts (tstr);
				free (tstr);
				item->append_flag = 1;
			}
		}
		else if (strcmp (context->tagName, "item")) {
			char *new_extra = (char *) malloc (
					(item->extra ? strlen(item->extra) : (size_t) 0)
					+ strlen(context->tagName) + (size_t) (len + 5));
			if (!new_extra) {
				msg_echo ("malloc() failed in rss_cdata() on extra element "
						"parsing", NULL);
				item->error = 1;
				return;
			}

			if (item->extra) {
				strcpy(new_extra, item->extra);
				if (!item->append_flag)
					strcat (strcat(strcat(new_extra, "\n"), context->tagName), ": ");
			} else {
				strcat (strcpy(new_extra, context->tagName), ": ");
			}
			item->append_flag = 1;

			*((char *)(mempcpy(new_extra+strlen(new_extra), str, len))) = '\0';

			if (item->extra) free (item->extra);

			item->extra = new_extra;
		}
	}
}

void XMLCALL rss_closetag(void *data, const char *element) {
	//puts("rss_closetag");
	rss_context context;
	context = (rss_context) data;
	XML_Parser parser = context->parser;
//	fflush (stdout); fputs ("<<<CLOSING TAG>>>", stderr); fputs (element, stderr); fputc ('\n', stderr); fflush (stderr);
	
	/* take away context */
	XML_SetUserData (parser, context->prev);

	/* for a "category" element */
	if (context->item)
	  context->item->append_flag = 0;

	/* do things with an item when it's completed */
	if (!strcmp (element, "item")) {
		record_item (context->item);
		free (context->item);
	}

	rssth_destroy_context (context);
}

void set_parser_callbacks(XML_Parser parser) {
	XML_SetXmlDeclHandler (parser, rss_xml);
	XML_SetElementHandler (parser, rss_opentag, rss_closetag);
	XML_SetCharacterDataHandler (parser, rss_cdata);
	XML_SetUnknownEncodingHandler (parser, rss_unknown_enc, NULL);
}

void clear_item (rss_item item) {
	//puts ("clear-item");
	if (item->title && !item->static_title) free (item->title);
	if (item->link) free (item->link);
	if (item->description && !item->static_description) free (item->description);
	if (item->pubDate) free (item->pubDate);
	if (item->guid) free (item->guid);
	if (item->extra) free (item->extra);
	if (item->cats) free (item->cats);
}

void try_insert (PGconn *db, char *table, char* id, char *field, char *value, char *spare) {
	char sql[LINE_MAX];
	concat (sql, "UPDATE ", table,
			" SET ", field, " = $1 WHERE ID = ", id, NULL);

	if (!db_exec (db, sql, 1, value))
		db_exec (db, sql, 1, spare);
}

int record_item (const rss_item item) {
	if (item->error) {
		msg_echo ("Item will not be recorded due to parsing error", NULL);
		if (item->title)
			msg_echo ("\tItem title:", item->title, NULL);
		clear_item (item);
		return 0;
	}

	if (!item->title) {
		item->static_title = 1;
		item->title = "<NO TITLE>";
	}

	if (!item->description) {
		item->static_description = 1;
		item->description = "<NO DESCRIPTION>";
	}

	char buf[LINE_MAX];
	char *table = item->context->sel->table;
	PGconn *db = item->context->db;
	PGresult *res;
	time_t pubDate = 0;

	if (item->pubDate) {
		char *cursor = item->pubDate;
		while (*cursor != '\0') {
			if (!isascii ((int) *cursor)){
				free (item->pubDate);
				item->pubDate = NULL;
				break;
			}
			cursor++;
		}
	}

	char *entry = NULL;
	char _catsbuf[LINE_MAX], *catsbuf = _catsbuf;
	if (item->cats) {
		strcpy (catsbuf, entry = argz_next(item->cats, item->cats_len, entry));
		while (entry = argz_next(item->cats, item->cats_len, entry)) {
			strcat (catsbuf, ", ");
			strcat (catsbuf, entry);
		}
	} else catsbuf = NULL;

	const char *parmValues[7];   /* seven just for the next use */
	int nparms = 0;
	if (!item->pubDate) {
		char *sql = NULL, *uniq_field, *uniq_field_ptr, *verbosity_level;
		append (&sql, concat(buf,
					"SELECT ID FROM ", table, " WHERE ", NULL));
		if (item->guid) {
			append (&sql, concat(buf, "GUID = '", item->guid, "'", NULL));
			verbosity_level = "DEBUG";
			uniq_field = "GUID";
			uniq_field_ptr = item->guid;
		} else if (item->title) {
			verbosity_level = "VERBOSE";
			append (&sql, concat(buf, "Title = '", item->title, "'", NULL));
			uniq_field = "Title";
			uniq_field_ptr = item->title;
		} else {
			msg_echo ("No way to determine the item uniqueness. " 
					"Recording is aborted.", NULL);
			free (sql);
			clear_item (item);
			return 0;
		}

		res = db_exec (db, sql, 0);
		free (sql);
		if (!res) {
			clear_item (item);
			return 0;
		}
		//printf ("ntuples: %d\n", PQntuples(res));
		if (PQntuples (res)) {
			msg_echo (verbosity_level, "Duplicated item with no date.",
					uniq_field, "=", uniq_field_ptr, NULL);
			PQclear (res);
			clear_item (item);
			return 0;
		}
		PQclear (res);
	}
	
	if (item->context->lastRecDate && item->pubDate) {
		pubDate = parse_time(item->pubDate);
		if (pubDate < item->context->lastRecDate) {
			clear_item (item);
			return 1;
		} else if (item->guid) {
			if (!(res = db_exec (db, concat(buf, "SELECT COUNT(ID) FROM ",
					table, " WHERE GUID = $1", NULL), 1, item->guid))) {
				clear_item (item);
				return 0;
			}
			
			int cmp = strcmp(PQgetvalue(res, 0, 0), "0");
			PQclear (res);
			if (!cmp) {
				msg_echo ("DEBUG",
						"Duplicated item. GUID =", item->guid, NULL);
				clear_item (item);
				return 0;
			}
		}
	}
	
	char id[20];
	parmValues[0] = item->title;
	parmValues[1] = item->link;
	parmValues[2] = item->description;
	parmValues[3] = item->pubDate;
	parmValues[4] = item->guid;
	parmValues[5] = catsbuf;
	parmValues[6] = item->extra;
	res = PQexecParams(db, concat (buf, "INSERT INTO ",
				table, " (Title, Link, Description, "
				"PubDate, GUID, RecDate, Categories, ExtraElements) "
				"VALUES ($1, $2, $3, $4, $5, CURRENT_TIMESTAMP, $6, $7)", NULL),
			7, NULL, parmValues, NULL, NULL, 0);

	if (res) {
		ExecStatusType status = PQresultStatus (res);
		char *sqlstate;
		switch (status) {
			case PGRES_FATAL_ERROR:
				sqlstate = PQresultErrorField (res, PG_DIAG_SQLSTATE);
				if (!strcmp(sqlstate, "22P05")) {    /*UNTRANSLATABLE CHARACTER*/
					#define SPARE_VALUE "<ENCODING CONVERSION ERROR>"
					db_exec (db, concat (buf, "INSERT INTO ", table, " (RecDate) "
								"VALUES (CURRENT_TIMESTAMP)", NULL), 0);
					strcpy (id, PQgetvalue (db_exec (db, concat (buf,
						"SELECT CURRVAL('", table, "_id_seq')", NULL), 0), 0, 0));
					try_insert (db, table, id, "Title", item->title, SPARE_VALUE);
					try_insert (db, table, id, "Link", item->link, SPARE_VALUE);
					try_insert (db, table, id, "Description", item->description, SPARE_VALUE);
					try_insert (db, table, id, "PubDate", item->pubDate, SPARE_VALUE);
					try_insert (db, table, id, "GUID", item->guid, SPARE_VALUE);
					try_insert (db, table, id, "Categories", catsbuf, SPARE_VALUE);
					try_insert (db, table, id, "ExtraElements", item->extra, SPARE_VALUE);
				}
				break;
		}
		if (status != PGRES_COMMAND_OK) {
			msg_echo ("Item recording error:", PQresStatus(status),
					"\n\tItem GUID:", item->guid,
					"\n\tSQLSTATE code:",
					PQresultErrorField(res, PG_DIAG_SQLSTATE), 
					"\n\t", PQresultErrorMessage (res), NULL);
			PQclear (res);
			clear_item (item);
			return 0;
		}
		PQclear (res);
	} else {
		msg_echo ("SQL command execution error during item recording:",
				PQerrorMessage (db), NULL);
		clear_item (item);
		return 0;
	}

	if (res = db_exec (db, concat (buf,
					"SELECT CURRVAL('", table, "_id_seq')", NULL), 0)) {
		strcpy (id, PQgetvalue (res, 0, 0));
		PQclear (res);
	} else {	
		clear_item (item);
		return 0;
	}

	if (item->cats) {
		entry = NULL;
		while (entry = argz_next(item->cats, item->cats_len, entry)) {
			db_exec (db, concat (buf,
						"INSERT INTO ", table, "_Categories (Item, Category) "
						"VALUES ($1, $2)", NULL), 2, id, entry);
		}
	}

	clear_item (item);
	return 1;
}
