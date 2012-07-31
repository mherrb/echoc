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
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <err.h>
#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
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
int disconnected;
struct timeval sent;

#define THRESHOLD 10

static void
usage(void)
{
	errx(2, "usage: echoc [-v] server");
}

static void
send_packet(int unused)
{
	if (sendto(sock, &seq, sizeof(seq), 0, server,
		serverlen) != sizeof(seq)) {
		if (verbose)
			warn("sendto");
	}
	if (verbose > 1) 
		printf("sent %d\n", seq);
	seq++;
}

int
main(int argc, char *argv[])
{
	char name[NI_MAXHOST];
	char buf[80];
	struct sockaddr_storage client;
	struct addrinfo hints, *res, *res0;
	struct itimerval itv;
	struct tm *tm;
	struct pollfd pfd[1];
	socklen_t addrlen;
	int ch;
	int nfds, received = 0;
	int error, buffer, last;
	extern int optind;

	setbuf(stdout, NULL);
	while ((ch = getopt(argc, argv, "v")) != -1) {
		switch (ch) {
		case 'v':
			verbose++;
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

	disconnected = 1;
	memset(&client, 0, sizeof(client));

	/* timer values */
	itv.it_interval.tv_usec = 100000; /* 100ms */
	itv.it_interval.tv_sec = 0;
	itv.it_value.tv_usec = 100000;
	itv.it_value.tv_sec = 0;
	if (setitimer(ITIMER_REAL, &itv, NULL) == -1) 
		err(2, "setitimer");

	signal(SIGALRM, send_packet);

	/* send initial packet */
	gettimeofday(&sent, NULL);
	send_packet(0);

	while (1) {
		struct timeval now, diff;

		while (1) {
			pfd[0].fd = sock;
			pfd[0].events = POLLIN;
			nfds = poll(pfd, 1, 200);
			gettimeofday(&now, NULL);
			timersub(&now, &sent, &diff);
			if (nfds > 0)
				break;
			if (nfds == -1 && errno != EINTR)
				warn("poll error");
			if (verbose > 2) 
				printf("%d %s\n", nfds, strerror(errno));
			if (verbose > 1) 
				printf("wait %ld.%06ld\n", 
				    (long)diff.tv_sec, diff.tv_usec);
			if (diff.tv_sec > 0 || diff.tv_usec > 500000) {
				if (verbose) 
					printf("timeout %ld.%06ld\n", 
					    (long)diff.tv_sec, diff.tv_usec);
				disconnected++;
				nfds = 0;
				break;
			}
		}
		if ((nfds == 0)) {
			if (verbose > 1) 
				printf("!! %d packet(s) dropped\n", seq - last);
			if (disconnected == 1) {
				tm = localtime((time_t *)&now.tv_sec);
				strftime(buf, sizeof(buf), "%F %T", tm);
				printf("%s.%06ld: lost connection\n",
				    buf, now.tv_usec);
			}
			continue;
		}
		if (ioctl(sock, FIONREAD, &received) == -1)
			warn("ioctl FIONREAD");
		if (verbose > 1)
			printf("received %d bytes ", received);
		addrlen = sizeof(client);
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
		if (disconnected >= THRESHOLD) {
			tm = localtime((time_t *)&now.tv_sec);
			strftime(buf, sizeof(buf), "%F %T", tm);
			printf("%s.%06ld: connection is back\n",
			    buf, now.tv_usec);
			if (verbose)
				printf("dropped %d paquets\n", seq - last);
			exit(3);
		}
		disconnected = 0;
		last = buffer;
		gettimeofday(&sent, NULL);
		if (verbose > 1)
			printf("%d %ld.%06ld\n", buffer, (long)diff.tv_sec, 
			    diff.tv_usec);
	}
	close(sock);
	exit(0);
}
