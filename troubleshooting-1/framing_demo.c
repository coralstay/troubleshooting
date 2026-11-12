// ---
// title: framing_demo
// description: 같은 클라이언트 바이트 패턴을 read() 한 번을 메시지 하나로 보는 서버와 재조립하는 서버에 보내 short read 결함을 보여 주는 데모
// authors: [coralstay]
// license: AGPL-3.0-only
// status: draft
// task: TASK-1
// created_date: '2026-11-06'
// updated_date: '2026-11-06'
// ---
// [설계 메모]
// framing_demo.c - SSH agent style framing (uint32 big-endian length + body)
// over an AF_UNIX SOCK_STREAM socket.
//
// Sends the same client byte patterns to two servers:
//   buggy_serve(): treats one read() as one message (what Secretive 4.0.0 does)
//   fixed_serve(): accumulates bytes, extracts every complete message,
//                  keeps the remainder, caps the length (what OpenSSH ssh-agent does)
//
// Build and run:  cc -Wall -Wextra -O2 -o framing_demo framing_demo.c && ./framing_demo

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
        ssize_t r = read(fd, buf, sizeof(buf));  // ---- 1
        if (r <= 0)
            return;                                 /* EOF or error */
        if (r <= 4) {  // ---- 2
            printf("      x parse failed (read only %zd bytes) -> close\n", r);
            return;                                 /* close without a reply */
        }
        uint32_t len;
        memcpy(&len, buf, 4);
        len = ntohl(len);
        size_t body = (size_t)r - 4 < len ? (size_t)r - 4 : len;  // ---- 3
        printf("      %.*s%s\n", (int)body, (char *)buf + 4,
               body < len ? "  (truncated)" : "");
    }
}

/* ------------------------------------------------------------------ fixed */
/* Accumulate, extract every complete message, keep the remainder. */
static void fixed_serve(int fd)
{
    static unsigned char acc[4 + MAX_LEN];          /* room for one maximum-size message */
    size_t used = 0;                                /* bytes currently held in acc */

    for (;;) {
        ssize_t r = read(fd, acc + used, sizeof(acc) - used);  // ---- 4
        if (r == 0)
            return;                                 /* EOF: drop an incomplete message */
        if (r < 0) {
            if (errno == EINTR)
                continue;
            return;
        }
        used += (size_t)r;

        size_t off = 0;
        while (used - off >= 4) {  // ---- 5
            uint32_t len;
            memcpy(&len, acc + off, 4);
            len = ntohl(len);
            if (len > MAX_LEN) {  // ---- 6
                printf("      x length %u exceeds cap -> close\n", len);
                return;
            }
            if (used - off < 4 + (size_t)len)
                break;  // ---- 7
            printf("      %.*s\n", (int)len, (char *)acc + off + 4);
            off += 4 + len;
        }
        memmove(acc, acc + off, used - off);  // ---- 8
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
        write_all(fd, m, 4);  // ---- 9
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


// ======== 노트 ========
// 1 read() 한 번을 메시지 하나로 본다
//   설명: 소켓은 바이트 스트림이라 read가 돌려주는 크기는 메시지 경계와 무관하다. 이 서버는 한 번의 read(최대 sizeof(buf)바이트)가 곧 완성된 메시지라고 가정하는데, 이것이 short read 결함의 뿌리다 (Secretive 4.0.0의 동작).
//   관련: troubleshooting-1.md, secretive_repro.py
//   개념: short read, 바이트 스트림
// 2 4바이트 이하면 파싱 실패로 끊는다
//   설명: Secretive의 data.count > 4 검사(아니면 .invalidData)를 흉내 낸다. 길이만 먼저 도착한 패턴 A에서 여기에 걸려 응답 없이 연결을 닫고, 클라이언트는 "communication with agent failed"를 본다.
// 3 도착한 만큼만 자른다
//   설명: 선언된 길이보다 적게 도착하면 도착한 바이트만 본문으로 취급해 "(truncated)"로 표시한다 (패턴 B).
//   설명: 이 메시지 뒤에 이어진 바이트(두 번째 메시지)는 버려진다 (패턴 C에서 LIST_KEYS만 처리되고 SIGN_REQUEST는 사라진다).
// 4 누적 버퍼 뒤에 이어 붙여 읽는다
//   설명: 고친 서버의 1단계. 읽은 바이트를 acc의 used 뒤에 덧붙인다. 버퍼는 최대 크기 메시지 하나(4 + MAX_LEN)가 들어갈 만큼이다. read가 0이면 EOF라 미완성 메시지는 버린다.
//   관련: secretive_repro.py (같은 로직의 재조립 릴레이)
// 5 완성된 메시지를 모두 꺼낸다
//   설명: 2단계. 길이 4바이트가 모였으면 길이를 읽고 본문이 다 모인 메시지를 하나씩 꺼낸다. 한 번의 read에 메시지가 여럿 들어 있어도(패턴 C) 모두 처리된다.
// 6 길이 상한 검사
//   설명: 상한(MAX_LEN, OpenSSH AGENT_MAX_LEN과 같은 256KiB)을 넘는 길이는 거짓으로 보고 연결을 닫는다 (패턴 D). 검사가 없으면 거대한 길이를 기다리며 메모리와 연결을 붙잡는다.
//   개념: 입력 검증, 상한
// 7 본문이 덜 왔으면 더 읽는다
//   설명: 남은 바이트가 4 + len보다 적으면 이 메시지는 미완성이므로 꺼내지 않고 루프를 빠져나와 다음 read를 기다린다 (패턴 A, B가 여기서 정상 처리된다).
// 8 남은 바이트를 앞으로 당겨 보존한다
//   설명: 3단계. 꺼내고 남은 미완성 조각을 버퍼 맨 앞으로 옮기고 used를 줄여, 다음 read가 그 뒤에 이어 붙게 한다. 이 보존이 있어야 메시지가 여러 read에 걸쳐도 이어진다.
// 9 길이를 따로 쓰고 쉰다
//   설명: ssh-keygen처럼 길이 4바이트만 먼저 쓰고 100ms 뒤 본문을 쓴다 (패턴 A). 서버의 read가 길이만 읽고 돌아오게 만드는 재현 입력이다. 패턴 B는 길이 + 본문 일부 뒤에 같은 간격을 둔다.
