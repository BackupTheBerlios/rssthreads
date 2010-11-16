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

PGconn * db_connect (const struct selector *sel) {
	PGconn *db = PQconnectdb (sel->dbKeys);
	if (PQstatus(db) == CONNECTION_BAD) {
		msg_echo ("Can't connect to database server:",
				PQerrorMessage (db), NULL);
		PQfinish (db);
		return (PGconn *)NULL;
	} else {
		if (!db_exec (db, "SET search_path TO rssthreads", 0))
			return (PGconn *)NULL;
		return db;
	}
}

PGresult * db_exec (PGconn *db, const char *sql, unsigned int nParms, ...) {
	PGresult *res;
	if (nParms) {
		const char *parmValues[nParms];
		va_list params;

		va_start (params, nParms);
		int i;
		for (i=0; i<nParms; i++) 
			parmValues[i] = va_arg (params, char*);
		va_end (params);

		res = PQexecParams (db, sql, nParms, NULL, parmValues, NULL, NULL, 0);
	} else {
		res = PQexec (db, sql);
	}

	if (res) {
		ExecStatusType status;
		switch (status = PQresultStatus (res)) {
			case PGRES_COMMAND_OK:
				PQclear (res);
				res = (PGresult *) 1;
				break;
			case PGRES_TUPLES_OK:
				break;
			default:
				msg_echo ("SQL command execution",
						PQresultErrorMessage (res), NULL);
				msg_echo ("\tSQL:", sql, NULL);
				PQclear (res);
				res = NULL;
		}
	} else {
		msg_echo ("SQL command execution error:", PQerrorMessage (db), NULL);
		msg_echo ("\tSQL:", sql, NULL);
	}

	return res;
}

int db_create (const struct selector *sel) {
	PGconn *db;
	char *url = sel->link;
  	char *table = sel->table;
	char *interval = sel->interval ? sel->interval : RSSTH_DEFAULT_INTERVAL;
	char *description = sel->description;

	if (db = db_connect(sel)) {
		char buf[LINE_MAX];
		if ( !db_exec (db,
					"INSERT INTO RSS (URL, Interval, TableName, Description) "
					"VALUES ($1, $2, $3, $4)",
					4, url, interval, table, description)) {
			msg_echo ("Failed to register new feed.", 
					"Maybe, feed with URL", url, "is already registered.", NULL);
			db_close (db);
			return 0;
		}
		if (!table) {
			db_exec (db, "UPDATE RSS SET TableName = 'RSS' || ID "
					"WHERE URL = $1", 1, url);
		}
		if (	!db_exec (db, concat (buf, "CREATE TABLE ",
						table, "_Categories (Item integer, Category text)",
						NULL), 0) ||
					/*	prefix, "_Categories (Item serial PRIMARY KEY, Name text)",
						NULL), 0) ||
				!db_exec (db, concat (buf, "CREATE TABLE ",
						prefix, "_CategoryIndex (Item integer, Category integer)",
						NULL), 0) ||*/
				!db_exec (db, concat (buf, "CREATE TABLE ",
						table, " (ID serial PRIMARY KEY, "
						"Title text, "
						"Link text, "
						"Description text, "
						"PubDate timestamp(0) with time zone, "
						"RecDate timestamp(0) with time zone, "
						"GUID text, "
						"Categories text, "
						"ExtraElements text, "
						"ReadMark boolean NOT NULL DEFAULT False, "
						"PrimaryMark boolean NOT NULL DEFAULT False, "
						"SecondaryMark boolean NOT NULL DEFAULT False, "
						"DeleteMark boolean NOT NULL DEFAULT False"
						/*"UNIQUE (Title, Link, Description, PubDate, Categories)"*/
						")",
						NULL), 0) ) {
			db_close (db);
			return 0;
		}
		return 1;
	} else {
		return 0;
	}
}

int db_info (struct selector *sel) {
	PGconn *db;
	char *sql = NULL;

	if (!(db = db_connect(sel))) return EXIT_FAILURE;

	switch (sel->qualifier) {
		case qlf_url:
			append (&sql, "SELECT * FROM RSS WHERE URL = '");
			append (&sql, sel->link);
			append (&sql, "'");
			break;
		case qlf_table:
			append (&sql, "SELECT * FROM RSS WHERE TableName = '");
			append (&sql, sel->table);
			append (&sql, "'");
			break;
		case qlf_id:
			append (&sql, "SELECT * FROM RSS WHERE ID = ");
			append (&sql, sel->id);
			break;
		case qlf_none:
			append (&sql, "SELECT ID, TableName, URL FROM RSS ORDER BY ID");
			break;
	}

	PGresult *res = db_exec (db, sql, 0);
	free (sql);
	db_close (db);

	if (!res) return EXIT_FAILURE;

	int ntuples, row;
	if (ntuples = PQntuples (res)) {
		if (sel->qualifier == qlf_none) {
			for (row = 0; row < ntuples; row++) {
				printf ("%6u   %-20s %-s\n",
						strtoul (PQgetvalue (res, row, 0), NULL, 0),
						PQgetvalue (res, row, 1),
						PQgetvalue (res, row, 2));
			}
		} else {
			#define get_field(FNAME) PQgetvalue(res, 0, PQfnumber(res, FNAME))
			printf ("          ID: %s\n", get_field("ID"));
			printf ("         URL: %s\n", get_field("URL"));
			printf ("  TABLE NAME: %s\n", get_field("TableName"));
			printf (" DESCRIPTION: %s\n", get_field("Description"));
			printf ("    INTERVAL: %s\n", get_field("Interval"));
			char *bvar = get_field("Active");
			if (!strcmp (bvar, "t")) bvar = "yes";
			if (!strcmp (bvar, "f")) bvar = "no";
			printf ("      ACTIVE: %s\n", bvar);
		}
	}

	PQclear (res);
	return EXIT_SUCCESS;
}

int db_drop (struct selector *sel) {
	PGconn *db;
	PGresult *res;
	char table[33];
	char buf[LINE_MAX];

	if (!(db = db_connect(sel))) return EXIT_FAILURE;

	if (!obtain_table_name (table, db, sel)) return EXIT_FAILURE;

	int exitcond = (db_exec (db, concat(buf, "DROP TABLE ", table, NULL), 0) &&
	db_exec (db, concat(buf, "DROP TABLE ", table, "_Categories", NULL), 0) &&
	db_exec (db, "DELETE FROM RSS WHERE TableName = $1", 1, table));

	db_close (db);

	return exitcond ? EXIT_SUCCESS : EXIT_FAILURE;
}

int db_alter (struct selector *sel) {
	PGconn *db;
	char *sql = NULL;

	if (!(db = db_connect(sel))) return EXIT_FAILURE;

	append (&sql, "UPDATE RSS SET ");
	unsigned short prev = 0;
	if (sel->link2 && (sel->qualifier != qlf_url)) {
		append (&sql, "URL = '");
		append (&sql, sel->link2);
		append (&sql, "'");
		prev++;
	}
	if (sel->interval) {
		if (prev) append (&sql, ", ");
		append (&sql, "Interval = '");
		append (&sql, sel->interval);
		append (&sql, "'");
		prev++;
	}
	if (sel->active) {
		if (prev) append (&sql, ", ");
		append (&sql, "Active = '");
		append (&sql, sel->active);
		append (&sql, "'");
		prev++;
	}
	if (sel->description) {
		if (prev) append (&sql, ", ");
		append (&sql, "Description = '");
		append (&sql, sel->description);
		append (&sql, "'");
		prev++;
	}
	if (!prev) {
		free (sql);
		msg_echo ("Hm, so what to modify?", NULL);
		return EXIT_FAILURE;
	}

	append (&sql, " WHERE ");

	switch (sel->qualifier) {
		case qlf_url:
			append (&sql, "URL = '");
			append (&sql, sel->link);
			append (&sql, "'");
			break;
		case qlf_table:
			append (&sql, "TableName = '");
			append (&sql, sel->table);
			append (&sql, "'");
			break;
		case qlf_id:
			append (&sql, "ID = ");
			append (&sql, sel->id);
			break;
	}

	PGresult *res = db_exec (db, sql, 0);
	free (sql);
	db_close (db);

	return res ? EXIT_SUCCESS : EXIT_FAILURE;
}

int db_setup (void) {
	PGconn *db;
	if (db = PQsetdbLogin (NULL, NULL, NULL, NULL, NULL, NULL, NULL)) {
		if (!db_exec (db, "CREATE SCHEMA rssthreads; "
					"SET search_path TO rssthreads", 0))
			return 0;
		PGresult *res = db_exec (db, "CREATE TABLE RSS ("
				"ID serial, "
				"Active boolean NOT NULL DEFAULT true, "
				"URL text NOT NULL UNIQUE, "
				"Interval interval NOT NULL, "
				"TableName NOT NULL varchar(32), "
				"LastRecordDate timestamp(0) with time zone DEFAULT NULL, "
				"Description text, "
				"PRIMARY KEY (ID, URL)"
				")", 0);
		db_close (db);
		return (int) res;
	}
	fprintf (stderr, "Database connection error: %s\n", PQerrorMessage(db));
	return 0;
}

PGconn * db_open (rss_context context, const struct selector *sel) {
	if (!sel->link && !sel->table) {
		msg_echo ("Cannot operate when neither URL nor table was specified.",
				NULL);
		return (PGconn *) NULL;
	}

	//char *url = sel->link;
	PGconn *db;
	if (db = db_connect(sel)) {
		context->db = db;
		//PGresult *res;
		//char *prefix = (char *) malloc (33);
		//context->tablePrefix = prefix;
		//char strbuf[LINE_MAX];

#if 0	
		if (!sel->prefix) {
			res = db_exec (db,
					"SELECT TablePrefix FROM RSS WHERE URL = $1", 1, url);
			if (res) {
				if (PQntuples (res)) {
					strcpy (prefix, PQgetvalue (res, 0, 0));
					PQclear (res);
				} else {
					db_close (db);
					msg_echo ("RSS feed with URL", url,
							"is not registered.", NULL);
					db = (PGconn *) NULL;
				}
			} else {
				msg_echo ("Database query error:", PQerrorMessage (db),
						"\n\tURL:", url);
				db_close (db);
				db = (PGconn *) NULL;
			}
		} else {
			strcpy (prefix, sel->prefix);
		}

		if (db) {
			concat (strbuf, "INSERT INTO ",
					prefix, "_Feed VALUES ($1, $2, $3, $4, $5)", NULL);
			PQclear (PQprepare (db, "itemInsert", strbuf, 5, NULL));
		}
#endif

	}
	return db;
}

void db_close (PGconn *db) {
	PQfinish (db);
}
