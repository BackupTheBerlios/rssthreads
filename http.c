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

pthread_mutex_t rssth_http_mutex = PTHREAD_MUTEX_INITIALIZER;

int http_open (const struct selector *sel) {
	char * URL = sel->link;
	URL="http://localhost/hello/";
	char *url = malloc (strlen(URL) + 1);
	strcpy(url, URL);
	char *saveptr;
	strtok_r (url, "/", &saveptr);
	char *hostname = strtok_r (NULL, "/", &saveptr);
	char *location = strtok_r (NULL, "", &saveptr);
	int fd = socket(PF_INET, SOCK_STREAM, 6);
	struct sockaddr_in in_adrs;
	in_adrs.sin_family = AF_INET;
	struct hostent *hostinfo = gethostbyname (hostname);
	in_adrs.sin_addr = *(struct in_addr *) hostinfo->h_addr_list[0];
	in_adrs.sin_port = htons ((uint16_t)80);

	pthread_mutex_lock (&rssth_http_mutex);
	sleep (1);
	msg_debug ("connecting to", URL, NULL);
	if (connect (fd, (struct sockaddr *) &in_adrs,
				sizeof (in_adrs))) {
		perror ("connect");
		pthread_mutex_unlock (&rssth_http_mutex);
		return 0;
	}
	pthread_mutex_unlock (&rssth_http_mutex);
	
	FILE * netstream = fdopen (dup(fd), "r+");
	if (! netstream) {
		perror ("stream");
		return 0;
	}
	setbuf (netstream, NULL);
	
	fprintf (netstream, "GET /%s HTTP/1.0\n", location);
	fprintf (netstream, "Host: %s\n\n", hostname);

	char respstr[LINE_MAX];
	char * chrptr;
	fgets(respstr, LINE_MAX, netstream);
	if (!strstr (respstr, "200 OK")) {
		msg_echo ("The HTTP response is of not managed type.",
				"\n\tThread:", sel->table,
				"\n\tURL:", URL, 
				"\n\t---", NULL);
		do {
			*(strchr(respstr, 0x0a)) = ' ';
			if (chrptr = strchr(respstr, 0x0d)) *chrptr = ' ';
			msg_echo ("\t", respstr, NULL);
			fgets(respstr, LINE_MAX, netstream);
		} while (respstr[0] != 0x0a && respstr[0] != 0x0d);
		//pthread_exit (NULL);
		fd = 0;
	} else {
		do {
			*(strchr(respstr, 0x0a)) = ' ';
			if (chrptr = strchr(respstr, 0x0d)) *chrptr = ' ';
			msg_debug ("\t", respstr, NULL);
			fgets(respstr, LINE_MAX, netstream);
		} while (respstr[0] != 0x0a && respstr[0] != 0x0d);
	}
	
	free (url);
	fclose(netstream);

	return fd;
}
