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
#include "rhelp.h"

void append_list (char **sql, char *list, char *fieldname) {
	char *token;
	char buf[LINE_MAX];
	char *saveptr;

	append (sql, concat(buf, "(", fieldname, "='",
				strtok_r (list, ",", &saveptr), "'", NULL));

	while (token = strtok_r(NULL, ",", &saveptr))
		append (sql, concat(buf, " OR ", fieldname, "='",
					token, "'", NULL));

	append (sql, ")");
}

void append_mark (char **sql, short mark, char *fieldname) {
	append (sql, fieldname);
	switch (mark) {
		case 1:
			append (sql, " IS TRUE");
			break;
		case -1:
			append (sql, " IS FALSE");
			break;
	}
}

int rssth_read (struct selector *sel, unsigned short dump) {
	if (!sel->id && !sel->link && !sel->table) {
		msg_echo ("URL, table name or ID must be specified.", NULL);
		return EXIT_FAILURE;
	}
	
	PGconn *db;
	PGresult *res;
	char table [33]; /* table name */
	char *sql = NULL;
	char buf[LINE_MAX];

	if (!(db = db_connect(sel))) return EXIT_FAILURE;

	if (!obtain_table_name (table, db, sel)) return EXIT_FAILURE;
	
	append (&sql, concat(buf, "SELECT DISTINCT ",
				table, ".ID,",
				table, ".Title,",
				table, ".Link,",
				table, ".Description,",
				table, ".PubDate,",
				table, ".GUID,",
				table, ".RecDate,",
				table, ".Categories,",
				NULL));
	if (!sel->hideExtra)
		append (&sql, concat(buf, table, ".ExtraElements,", NULL));
	append (&sql, concat(buf, 
				table, ".ReadMark,",
				table, ".PrimaryMark,",
				table, ".SecondaryMark,",
				table, ".DeleteMark",
				" FROM ", table,
				NULL));
	if (!sel->tuple) {
		append_from_categories (&sql, sel, table);
		append_where_clause (&sql, sel, table);
		append_orderby_clause (&sql, sel);
	} else {
		char id_buf[20];
		if (!select_tuple (table, sel, db, id_buf)) {
			free (sql);
			db_close (db);
			return EXIT_FAILURE;
		}
		append (&sql, concat(buf, " WHERE ", table, ".ID = ", id_buf, NULL));
	}

	msg_debug ("SQL:", sql, NULL);

	if (!(res = db_exec (db, sql, 0))) {
		free (sql);
		db_close (db);
		return EXIT_FAILURE;
	}
	
	int nrows, row;
	if (nrows = PQntuples (res)) {

		#define get_field(FNAME) PQgetvalue(res, row, PQfnumber(res, FNAME))

		#define mark_item(MARK, VALUE) db_exec (db, \
				concat(buf, "UPDATE ", table, \
					" SET " MARK " = " VALUE " WHERE ID = $1", NULL), \
				1, get_field("ID"))

		for (row = 0; row < nrows; row++) {
			printf ("\n[%s] %s\n\n", get_field("ID"), get_field("Title"));
			printf ("DATE: %s\n", get_field("PubDate"));
			printf ("CATEGORIES: %s\n", get_field("Categories"));

			char *link = get_field("Link");
			printf ("%s\n\n", link);

			char *description = NULL;
			append (&description, get_field("Description"));
			if (sel->descfilter) {
				if (!pipe_output(description, sel->descfilter)) {
					free (description);
					free (sql);
					db_close (db);
					return EXIT_FAILURE;
				}
			} else {
				printf ("%s\n", word_wrap(&description));
			}
			free (description);

			printf ("\nGUID: %s\n", get_field("GUID"));

			if (!sel->hideExtra)
				printf ("%s\n", get_field("ExtraElements"));

			strcpy (buf, "");
			if (*(get_field("ReadMark")) == 't') strcat (buf, "R");
			if (*(get_field("PrimaryMark")) == 't') strcat (buf, "P");
			if (*(get_field("SecondaryMark")) == 't') strcat (buf, "S");
			if (*(get_field("DeleteMark")) == 't') strcat (buf, "D");
			if (strlen (buf))
				printf ("MARKS: %s\n", buf);
			
			printf ("%s   %d/%d\n",
					get_field("RecDate"), row+1, nrows);

			if (dump) {
				printf ("\n");
			} else {
				printf (": ");

				char answer[LINE_MAX], *cursor;
				fgets (answer, LINE_MAX-1, stdin);
				
				if (!sel->noread)
					mark_item ("ReadMark", "True");

				unsigned short backwind;
				backwind = 0;
				for (cursor = answer; *cursor != '\0'; cursor++) {
					switch (*cursor) {
						case 'r':
							puts ("Read mark setting.");
							mark_item ("ReadMark", "True");
							break;
						case 'R':
							puts ("Item unreading.");
							mark_item ("ReadMark", "False");
							break;
						case 'p':
							puts ("Primary mark setting.");
							mark_item ("PrimaryMark", "True");
							break;
						case 'P':
							puts ("Primary mark unsetting.");
							mark_item ("PrimaryMark", "False");
							break;
						case 's':
							puts ("Secondary mark setting.");
							mark_item ("SecondaryMark", "True");
							break;
						case 'S':
							puts ("Secondary mark unsetting.");
							mark_item ("SecondaryMark", "False");
							break;
						case 'd':
							puts ("Marking item for deletion.");
							mark_item ("DeleteMark", "True");
							break;
						case 'D':
							puts ("Delete mark clearing.");
							mark_item ("DeleteMark", "False");
							break;
						case '-':
							if (backwind) {
								row--;
							} else {
								row -= 2;
								backwind = 1;
							}
							break;
						case '+':
							row++;
							break;
						case 'b':
							if (!backwind) {
								row--;
								backwind = 1;
							}
							char *cl = malloc(strlen(sel->browser) + strlen(link) + 2);
							sprintf (cl, sel->browser, link);
							system (cl);
							free (cl);
							break;
						case 'h':
							draw_vline();
							printf (RSSTH_RHELP);
							draw_vline();
							printf ("Press Enter... ");
							getchar();
							putchar('\n');
							if (!backwind) {
								row--;
								backwind = 1;
							}
							break;
						case 'Q':
							puts ("Quit.");
							PQclear (res);
							free (sql);
							db_close (db);
							return EXIT_SUCCESS;
						default:
							if (!isspace ((int) *cursor))
								printf ("Unrecognized command: `%c'.\n", (int) *cursor);
					}
				}
				if (row < 0) row = -1;
			}
			if (row < (nrows - 1)) draw_vline();
		}
	}
	
	PQclear (res);
	free (sql);
	db_close(db);
	return EXIT_SUCCESS;
}

int print_links (struct selector *sel) {
	if (!sel->id && !sel->link && !sel->table) {
		msg_echo ("URL, table name or ID must be specified.", NULL);
		return EXIT_FAILURE;
	}
	
	PGconn *db;
	PGresult *res;
	char table[33];
	char *sql = NULL;
	char buf[LINE_MAX];
	//char tableName[38]; /* name of table with items (..._Feed) */

	if (!(db = db_connect(sel))) return EXIT_FAILURE;

	if (!obtain_table_name (table, db, sel)) return EXIT_FAILURE;
	//strcat (strcpy(tableName, prefix), "_Feed");

	append (&sql, concat(buf, "SELECT DISTINCT ",
				table, ".Link,",
				table, ".", (sel->sortField ? sel->sortField : "PubDate"),
				" FROM ", table, NULL));
	if (!sel->tuple) {
		append_from_categories (&sql, sel, table);
		append_where_clause (&sql, sel, table);
		append_orderby_clause (&sql, sel);
	} else {
		char id_buf[20];
		if (!select_tuple (table, sel, db, id_buf)) {
			free (sql);
			db_close (db);
			return EXIT_FAILURE;
		}
		append (&sql, concat(buf, " WHERE ", table, ".ID = ", id_buf, NULL));
	}

	if (!(res = db_exec (db, sql, 0))) {
		free (sql);
		db_close (db);
		return EXIT_FAILURE;
	}
	
	int nrows, row;
	if (nrows = PQntuples (res)) {

		for (row = 0; row < nrows; row++) {
			printf ("%s\n", PQgetvalue (res, row, 0));
		}
	}

	PQclear (res);
	free (sql);
	db_close (db);
	return EXIT_SUCCESS;
}

int set_marks (struct selector *sel) {
	PGconn *db;
	PGresult *res;
	char table[33];
	char *sql = NULL;
	char buf[LINE_MAX];

	if (!(db = db_connect(sel))) return EXIT_FAILURE;

	if (!obtain_table_name (table, db, sel)) return EXIT_FAILURE;

	append (&sql, concat(buf, "UPDATE ", table, " SET ", NULL));

	char *cursor;
	unsigned char multiple_marks = 0;

	#define appendAssignment(MARK, STATE) \
		if (multiple_marks++) \
			append (&sql, ", " MARK " = " STATE); \
		else \
			append (&sql, MARK " = " STATE);

	for (cursor = sel->markSet; *cursor != '\0'; cursor++) {
		switch (*cursor) {
			case 'r':
				appendAssignment("ReadMark", "True"); break;
			case 'R':
				appendAssignment("ReadMark", "False"); break;
			case 'p':
				appendAssignment("PrimaryMark", "True"); break;
			case 'P':
				appendAssignment("PrimaryMark", "False"); break;
			case 's':
				appendAssignment("SecondaryMark", "True"); break;
			case 'S':
				appendAssignment("SecondaryMark", "False"); break;
			case 'd':
				appendAssignment("DeleteMark", "True"); break;
			case 'D':
				appendAssignment("DeleteMark", "False"); break;
			default:
				fprintf (stderr, "Incorrect mark command: `%c'.\n", (int) *cursor);
				free (sql);
				db_close (db);
				return EXIT_FAILURE;
		}
	}

	if (!sel->tuple) {
		if (sel->cats || sel->noCats) 
			append (&sql, concat(buf, " FROM ", table, "_Categories", NULL));
		append_where_clause (&sql, sel, table);
	} else {
		char id_buf[20];
		if (!select_tuple (table, sel, db, id_buf)) {
			free (sql);
			db_close (db);
			return EXIT_FAILURE;
		}
		append (&sql, concat(buf, " WHERE ", table, ".ID = ", id_buf, NULL));
	}
	
	res = db_exec (db, sql, 0);
	
	free (sql);
	db_close (db);

	return res ? EXIT_SUCCESS : EXIT_FAILURE;
}
