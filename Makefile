# Copyright (C) 2010  Serge V. Baumer

# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, version 3 of the License.

# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.


CC = gcc -g
SRC = rssthreads.c parser.c http.c db.c tools.c util.c rssthreads.h \
		help.in rhelp.in
# OBJ = rssthreads.o parser.o http.o db.o tools.o util.o
OBJ_TH = rssthreads.o parser.o http.o db_th.o util_th.o
OBJ_PG = rsspg.o db_pg.o tools.o util_pg.o


all: rssthreads rsspg

rssthreads: $(OBJ_TH)
	$(CC) -lexpat -lpq -lpthread -o rssthreads $(OBJ_TH)

rsspg: $(OBJ_PG)
	$(CC) -lpq -o rsspg $(OBJ_PG)

rssthreads.o: rssthreads.c rssthreads.h th_help.h
	$(CC) -DRSS_TH -c rssthreads.c

rsspg.o: rsspg.c rssthreads.h help.h
	$(CC) -DRSS_PG -c rsspg.c

parser.o: parser.c rssthreads.h encodings.h
	$(CC) -DRSS_TH -c parser.c 

http.o: http.c rssthreads.h
	$(CC) -DRSS_TH -c http.c

db_pg.o: db.c rssthreads.h
	$(CC) -DRSS_PG -o db_pg.o -c db.c

db_th.o: db.c rssthreads.h
	$(CC) -DRSS_TH -o db_th.o -c db.c

tools.o: tools.c rssthreads.h rhelp.h
	$(CC) -DRSS_PG -c tools.c

util_pg.o: util.c rssthreads.h 
	$(CC) -DRSS_PG -o util_pg.o -c util.c

util_th.o: util.c rssthreads.h 
	$(CC) -DRSS_TH -o util_th.o -c util.c

help.h: help.in
	sed -e 's/\\/\\\\/g' -e 's/"/\\"/g' \
		-e '1s/^/\#define RSSTH_HELP "/' -e '2,$$s/^/"/' \
		-e 's/$$/\\n" \\/' \
		-e 's/	/\\t/' -e '$$s/ \\$$//' \
		help.in >help.h

th_help.h: th_help.in
	sed -e 's/\\/\\\\/g' -e 's/"/\\"/g' \
		-e '1s/^/\#define RSSTH_HELP "/' -e '2,$$s/^/"/' \
		-e 's/$$/\\n" \\/' \
		-e 's/	/\\t/' -e '$$s/ \\$$//' \
		th_help.in >th_help.h

rhelp.h: rhelp.in
	sed -e 's/\\/\\\\/g' -e 's/"/\\"/g' \
		-e '1s/^/\#define RSSTH_RHELP "/' -e '2,$$s/^/"/' \
		-e 's/$$/\\n" \\/' \
		-e 's/	/\\t/' -e '$$s/ \\$$//' \
		rhelp.in >rhelp.h

clean:
	rm rsspg rssthreads *.o help.h rhelp.h th_help.h ; true

.PHONY: all clean
