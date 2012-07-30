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
struct timespec sent;

#define THRESHOLD 10

static void
usage(void)
{
	errx(2, "usage: echoc [-v] server");
}

static void
send_packet(bool timestamp)
{
	if (sendto(sock, &seq, sizeof(seq), 0, server,
		serverlen) != sizeof(seq)) {
		if (verbose)
			warn("sendto");
	}
	if (timestamp) 
		clock_gettime(CLOCK_REALTIME, &sent);
	if (verbose > 1) 
		printf("sent %d\n", seq);
	seq++;
}

static void
timer_handler(int unused)
{
	send_packet(!disconnected);
}

static void
timespec_substract(struct timespec *result,
    const struct timespec *x, const struct timespec *y)
{
	struct timespec yy;

	memcpy(&yy, y, sizeof(struct timespec));

	/* Carry */
	if (x->tv_nsec < y->tv_nsec) {
		long sec = (y->tv_nsec - x->tv_nsec) / 1000000000 + 1;
		yy.tv_nsec -= 1000000000 * sec;
		yy.tv_sec += sec;
	}
	if (x->tv_nsec - y->tv_nsec > 1000000000) {
		int sec = (x->tv_nsec - y->tv_nsec) / 1000000000;
		yy.tv_nsec += 1000000000 * sec;
		yy.tv_sec -= sec;
	}

	result->tv_sec = x->tv_sec - yy.tv_sec;
	result->tv_nsec = x->tv_nsec - yy.tv_nsec;
}

int
main(int argc, char *argv[])
{
	char name[NI_MAXHOST];
	char buf[80];
	struct sockaddr_storage client;
	struct addrinfo hints, *res, *res0;
	struct timeval tv;
	struct itimerspec ts;
	struct sigevent se;
	struct sigaction sa;
	struct tm *tm;
	struct pollfd pfd[1];
	socklen_t addrlen;
	timer_t timer;
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
	gettimeofday(&tv, NULL);

	/* create a timer generating SIGALRM */
	memset(&se, 0, sizeof(struct sigevent));
	se.sigev_signo = SIGALRM;
	se.sigev_notify = SIGEV_SIGNAL;
	timer_create(CLOCK_REALTIME, &se, &timer);

	/* timer values */
	ts.it_interval.tv_nsec = 100000000; /* 100ms */
	ts.it_interval.tv_sec = 0;
	ts.it_value.tv_nsec = 100000000;
	ts.it_value.tv_sec = 0;
	timer_settime(timer, 0, &ts, NULL);

	/* set SIGALRM handler  */
	sa.sa_handler = timer_handler;
	sigemptyset(&sa.sa_mask);
	sigaddset(&sa.sa_mask, SIGALRM); /* block SIGALRM during handler */
	sigaction(SIGALRM, &sa, NULL);

	/* send initial packet */
	send_packet(true);

	while (1) {
		struct timespec now, diff;

		while (1) {
			pfd[0].fd = sock;
			pfd[0].events = POLLIN;
			nfds = poll(pfd, 1, 200);
			clock_gettime(CLOCK_REALTIME, &now);
			timespec_substract(&diff, &now, &sent);
			if (nfds > 0)
				break;
			if (nfds == -1 && errno != EINTR)
				warn("poll error");
			if (verbose > 2) 
				printf("%d %s\n", nfds, strerror(errno));
			if (verbose > 1) 
				printf("wait %ld.%06ld\n", 
				    (long)diff.tv_sec, diff.tv_nsec/1000);
			if (diff.tv_sec > 0 || diff.tv_nsec > 200000000) {
				if (verbose) 
					printf("timeout %ld.%06ld\n", 
					    (long)diff.tv_sec, diff.tv_nsec/1000);
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
				    buf, now.tv_nsec  * 1000);
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
			    buf, now.tv_nsec/1000);
			if (verbose)
				printf("dropped %d paquets\n", seq - last);
			exit(3);
		}
		disconnected = 0;
		last = buffer;
		if (verbose > 1)
			printf("%d %ld.%06ld\n", buffer, (long)diff.tv_sec, 
			    diff.tv_nsec/1000);
	}
	close(sock);
	exit(0);
}
