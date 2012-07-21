/*
 * Copyright (c) 2012 Matthieu Herrb <matthieu@herrb.eu>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <err.h>
#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

int sock = -1;
int verbose = 0;
struct sockaddr *server;
socklen_t serverlen;
unsigned int seq = 0;

#define THRESHOLD 10 

static void
usage(void)
{
	errx(2, "usage: echoc [-v] server");
}


int
main(int argc, char *argv[])
{
	char name[NI_MAXHOST];
	char buf[80];
	struct sockaddr_storage client;
	struct addrinfo hints, *res, *res0;
	struct timeval tv;
	struct tm *tm;
	struct pollfd pfd[1];
	socklen_t addrlen;
	int ch;
	int nfds, received = 0;
	int error, dropped, buffer, last;
	extern int optind;

	setbuf(stdout, NULL);
	while ((ch = getopt(argc, argv, "v")) != -1) {
		switch (ch) {
		case 'v':
			verbose = 1;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (argc != 1)
		usage();

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	error = getaddrinfo(argv[0], "echo", &hints, &res0);
	if (error)
		errx(1, "%s", gai_strerror(error));

	for (res = res0; res != NULL; res = res->ai_next) {
		sock = socket(res->ai_family, res->ai_socktype,
		    res->ai_protocol);
		if (sock != -1) {
			server = res->ai_addr;
			serverlen = res->ai_addrlen;
			break;
		}
	}
	if (sock == -1)
		err(1, "socket");

	dropped = 0;
	memset(&client, 0, sizeof(client));
	gettimeofday(&tv, NULL);
	while (1) {
		if (sendto(sock, &seq, sizeof(seq), 0, server,
			serverlen) != sizeof(seq)) {
			if (verbose)
				warn("sendto");
		}
		seq++;
		
		pfd[0].fd = sock;
		pfd[0].events = POLLIN;
		nfds = poll(pfd, 1, 200);
		if (nfds == -1 && errno != EINTR)
			warn("poll error");
		if ((nfds == 0) || (nfds == -1 && errno == EINTR)) {
			dropped++;
			if (dropped == THRESHOLD) {
				tm = localtime((time_t *)&tv.tv_sec);
				strftime(buf, sizeof(buf), "%F %T", tm);
				printf("%s.%06ld: lost connection\n", 
				    buf, tv.tv_usec);
			}
			continue;
		}

		if ((received = recvfrom(sock, &buffer, sizeof(buffer),
			    MSG_DONTWAIT,
			    (struct sockaddr *) &client,
			    &addrlen)) != sizeof(buffer)) {
			warn("recvfrom");
		}

		if (verbose && (serverlen != addrlen ||
			memcmp(&client, server, addrlen) != 0)) {
			if ((error = getnameinfo((struct sockaddr *)&client,
				addrlen, name, sizeof(name),
				    NULL, 0, NI_DGRAM)) != 0) {
				warnx("%s", gai_strerror(error));
			} else {
				warnx("received data from unknown host %s",
				    name);
			}
		}
		gettimeofday(&tv, NULL);
		if (dropped >= THRESHOLD) {
			tm = localtime((time_t *)&tv.tv_sec);
			strftime(buf, sizeof(buf), "%F %T", tm);
			printf("%s.%06ld: connection is back\n", 
			    buf, tv.tv_usec);
			if (verbose)
				printf("dropped %d paquets\n", dropped);
			dropped = 0;
		}
		last = buffer;
		if (verbose)
			printf("%d %ld.%06ld\n", buffer, tv.tv_sec, tv.tv_usec);
		usleep(100000);
	}
	close(sock);
	exit(0);
}
