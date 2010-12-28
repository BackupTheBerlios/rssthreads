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
OBJ = rssthreads.o parser.o http.o db.o tools.o util.o

rssthreads: $(OBJ)
	$(CC) -lexpat -lpq -lpthread -o rssthreads $(OBJ)
rssthreads.o: rssthreads.c rssthreads.h help.h
	$(CC) -c rssthreads.c
parser.o: parser.c rssthreads.h encodings.h
	$(CC) -c parser.c 
http.o: http.c rssthreads.h
	$(CC) -c http.c
db.o: db.c rssthreads.h
	$(CC) -c db.c
tools.o: tools.c rssthreads.h rhelp.h
	$(CC) -c tools.c
util.o: util.c rssthreads.h
	$(CC) -c util.c
help.h: help.in
	sed -e 's/\\/\\\\/g' -e 's/"/\\"/g' \
		-e '1s/^/\#define RSSTH_HELP "/' -e '2,$$s/^/"/' \
		-e 's/$$/\\n" \\/' \
		-e 's/	/\\t/' -e '$$s/ \\$$//' \
		help.in >help.h
rhelp.h: rhelp.in
	sed -e 's/\\/\\\\/g' -e 's/"/\\"/g' \
		-e '1s/^/\#define RSSTH_RHELP "/' -e '2,$$s/^/"/' \
		-e 's/$$/\\n" \\/' \
		-e 's/	/\\t/' -e '$$s/ \\$$//' \
		rhelp.in >rhelp.h

clean:
	rm rssthreads *.o help.h rhelp.h
