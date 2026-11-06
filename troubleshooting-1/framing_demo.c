/*
 * framing_demo.c - SSH agent style framing (uint32 big-endian length + body)
 * over an AF_UNIX SOCK_STREAM socket.
 *
 * Sends the same client byte patterns to two servers:
 *   buggy_serve(): treats one read() as one message (what Secretive 4.0.0 does)
 *   fixed_serve(): accumulates bytes, extracts every complete message,
 *                  keeps the remainder, caps the length (what OpenSSH ssh-agent does)
 *
 * Build and run:  cc -Wall -Wextra -O2 -o framing_demo framing_demo.c && ./framing_demo
 */
#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define SOCKET_PATH "/tmp/framing_demo.sock"
#define MAX_LEN     (256 * 1024)        /* same cap as OpenSSH AGENT_MAX_LEN */

/* ------------------------------------------------------------------ buggy */
/* One read() == one message. */
static void buggy_serve(int fd)
{
    unsigned char buf[1024];

    for (;;) {
        ssize_t r = read(fd, buf, sizeof(buf));     /* up to sizeof(buf) bytes */
        if (r <= 0)
            return;                                 /* EOF or error */
        if (r <= 4) {                               /* Secretive: data.count > 4 else .invalidData */
            printf("      x parse failed (read only %zd bytes) -> close\n", r);
            return;                                 /* close without a reply */
        }
        uint32_t len;
        memcpy(&len, buf, 4);
        len = ntohl(len);
        size_t body = (size_t)r - 4 < len ? (size_t)r - 4 : len;   /* truncate to what arrived */
        printf("      %.*s%s\n", (int)body, (char *)buf + 4,
               body < len ? "  (truncated)" : "");
        /* any bytes after this message (a second message) are dropped */
    }
}

/* ------------------------------------------------------------------ fixed */
/* Accumulate, extract every complete message, keep the remainder. */
static void fixed_serve(int fd)
{
    static unsigned char acc[4 + MAX_LEN];          /* room for one maximum-size message */
    size_t used = 0;                                /* bytes currently held in acc */

    for (;;) {
        ssize_t r = read(fd, acc + used, sizeof(acc) - used);   /* 1) append */
        if (r == 0)
            return;                                 /* EOF: drop an incomplete message */
        if (r < 0) {
            if (errno == EINTR)
                continue;
            return;
        }
        used += (size_t)r;

        size_t off = 0;
        while (used - off >= 4) {                   /* 2) extract complete messages */
            uint32_t len;
            memcpy(&len, acc + off, 4);
            len = ntohl(len);
            if (len > MAX_LEN) {                    /*    cap: refuse absurd lengths */
                printf("      x length %u exceeds cap -> close\n", len);
                return;
            }
            if (used - off < 4 + (size_t)len)
                break;                              /*    incomplete: read more */
            printf("      %.*s\n", (int)len, (char *)acc + off + 4);
            off += 4 + len;
        }
        memmove(acc, acc + off, used - off);        /* 3) keep the remainder */
        used -= off;
    }
}

/* ---------------------------------------------------------------- client */
static void write_all(int fd, const void *p, size_t n)
{
    const unsigned char *b = p;
    while (n > 0) {                                 /* write() may also be short */
        ssize_t w = write(fd, b, n);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            printf("      client write failed: %s\n", strerror(errno));
            return;
        }
        b += w;
        n -= (size_t)w;
    }
}

static size_t frame(unsigned char *out, const char *s)
{
    uint32_t len = htonl((uint32_t)strlen(s));
    memcpy(out, &len, 4);
    memcpy(out + 4, s, strlen(s));
    return 4 + strlen(s);
}

enum pattern { SPLIT_HEADER, SPLIT_BODY, TWO_AT_ONCE, HUGE_LENGTH };

static void client_send(int fd, enum pattern p)
{
    unsigned char m[64], two[128];
    size_t n = frame(m, "SIGN_REQUEST");

    switch (p) {
    case SPLIT_HEADER:                              /* like ssh-keygen: length, gap, body */
        write_all(fd, m, 4);
        usleep(100 * 1000);
        write_all(fd, m + 4, n - 4);
        break;
    case SPLIT_BODY:                                /* length + part of body, gap, rest */
        write_all(fd, m, 8);
        usleep(100 * 1000);
        write_all(fd, m + 8, n - 8);
        break;
    case TWO_AT_ONCE: {                             /* two messages in one write */
        size_t a = frame(two, "LIST_KEYS");
        size_t b = frame(two + a, "SIGN_REQUEST");
        write_all(fd, two, a + b);
        break;
    }
    case HUGE_LENGTH: {                             /* lie about the length */
        uint32_t len = htonl(0x7fffffff);
        write_all(fd, &len, 4);
        break;
    }
    }
}

/* ------------------------------------------------------------------- run */
struct server_arg {
    int listen_fd;
    void (*serve)(int);
};

static void *server_thread(void *p)
{
    struct server_arg *a = p;
    int fd = accept(a->listen_fd, NULL, NULL);
    if (fd >= 0) {
        a->serve(fd);
        close(fd);
    }
    return NULL;
}

static void run(const char *label, void (*serve)(int), enum pattern p)
{
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);
    unlink(SOCKET_PATH);

    int lfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (lfd < 0 || bind(lfd, (struct sockaddr *)&addr, sizeof(addr)) < 0 || listen(lfd, 1) < 0) {
        perror("server setup");
        exit(EXIT_FAILURE);
    }
    printf("    %s\n", label);
    fflush(stdout);

    struct server_arg arg = { lfd, serve };
    pthread_t t;
    pthread_create(&t, NULL, server_thread, &arg);

    int cfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (cfd < 0 || connect(cfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("client connect");
        exit(EXIT_FAILURE);
    }
    client_send(cfd, p);
    usleep(200 * 1000);
    shutdown(cfd, SHUT_WR);                         /* EOF to the server */
    pthread_join(t, NULL);
    close(cfd);
    close(lfd);
    unlink(SOCKET_PATH);
    fflush(stdout);
}

int main(void)
{
    signal(SIGPIPE, SIG_IGN);                       /* report EPIPE instead of dying */
    const char *names[] = {
        "A. length | 100 ms | body   (ssh-keygen pattern)",
        "B. length + 4 body bytes | 100 ms | rest",
        "C. two messages in one write",
        "D. length 0x7fffffff, no body",
    };
    for (int i = 0; i < 4; i++) {
        printf("\n%s\n", names[i]);
        run("buggy:", buggy_serve, (enum pattern)i);
        run("fixed:", fixed_serve, (enum pattern)i);
    }
    return 0;
}
