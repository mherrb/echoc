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

static void
usage(void)
{
	errx(2, "usage: echoc [-c nbr][-i ms][-t ms][-v] server");
}

static void
send_packet(int unused)
{
	if (sendto(sock, &seq, sizeof(seq), 0, server,
		serverlen) != sizeof(seq)) {
		if (verbose)
			warn("sendto");
	}
	if (verbose)
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
	struct timeval last_ts;
	struct timeval now, diff, timeout_tv;
	struct tm *tm;
	struct pollfd pfd[1];
	socklen_t addrlen;
	long interval = 100; 	/* default interval (ms) */
	long timeout = 500;	/* default timeout (ms) */
	int ch;
	int nfds, received = 0;
	int error, buffer, last = -1;
	int disconnected;
	int we_count = 0, counter; /* don't loop forever */
	extern int optind;

	setbuf(stdout, NULL);
	while ((ch = getopt(argc, argv, "c:i:t:v")) != -1) {
		switch (ch) {
		case 'c':
			we_count++;
			counter = atoi(optarg);
			break;
		case 'i':
			interval = atoi(optarg);
			break;
		case 't':
			timeout = atoi(optarg);
			break;
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

	if (interval <= 0 || timeout <= 0)
		errx(2, "interval and timeout must be > 0");
	if (counter < 2)
		errx(2, "can't count down from nothing");
	/* force timeout >= interval */
	if (timeout < interval) {
		timeout = interval;
		warnx("adjusting timeout to %ld ms "
		    "(must be greater than interval)", timeout);
	}
	timeout_tv.tv_sec = timeout / 1000;
	timeout_tv.tv_usec = (timeout % 1000) * 1000;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	error = getaddrinfo(argv[0], "echo", &hints, &res0);
	if (error)
		errx(1, "%s: %s", argv[0], gai_strerror(error));

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

	disconnected = 0;
	memset(&client, 0, sizeof(client));

	/* timer values */
	itv.it_interval.tv_usec = interval*1000;
	itv.it_interval.tv_sec = 0;
	itv.it_value.tv_usec = interval*1000;
	itv.it_value.tv_sec = 0;
	if (setitimer(ITIMER_REAL, &itv, NULL) == -1)
		err(2, "setitimer");

	signal(SIGALRM, send_packet);

	gettimeofday(&last_ts, NULL);

	while (1) {
		/* poll() loop to handle interruptions by SIGALRM */
		while (1) {
			pfd[0].fd = sock;
			pfd[0].events = POLLIN;
			nfds = poll(pfd, 1, timeout);
			gettimeofday(&now, NULL);
			timersub(&now, &last_ts, &diff);
			if (nfds > 0)
				break;
			if (nfds == -1 && errno != EINTR)
				warn("poll error");
			if (!timercmp(&diff, &timeout_tv, <)) {
				disconnected++;
				nfds = 0;
				break;
			}
		}
		if ((nfds == 0)) {
			if (verbose)
				printf("%d packet(s) dropped in %ld.%06ld s\n",
				    seq - last, diff.tv_sec, diff.tv_usec);

			if (disconnected == 1) {
				tm = localtime((time_t *)&last_ts.tv_sec);
				strftime(buf, sizeof(buf), "%F %T", tm);
				printf("%s.%06ld: lost connection\n",
				    buf, last_ts.tv_usec);
			}
			continue;
		}
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
		if (disconnected) {
			tm = localtime((time_t *)&now.tv_sec);
			strftime(buf, sizeof(buf), "%F %T", tm);
			printf("%s.%06ld: connection is back "
			    "dropped %d packets\n",
			    buf, now.tv_usec, seq - last);
			disconnected = 0;
		}
		last = buffer;
		memcpy(&last_ts, &now, sizeof(struct timeval));
		if (verbose)
			printf("received %d %ld.%06ld\n", buffer,
			    (long)diff.tv_sec, diff.tv_usec);
		if (we_count) {
			counter--;
			if (counter == 0) {
				printf("all job done\n");
				break;
			}
		}
	}
	close(sock);
	exit(0);
}
