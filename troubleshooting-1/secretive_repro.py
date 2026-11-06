#!/usr/bin/env python3
# ---
# title: secretive_repro
# description: Secretive 에이전트의 short read("communication with agent failed")를 재현하고 수신 측 재조립으로 사라지는지 확인하는 스크립트
# authors: [coralstay]
# license: AGPL-3.0-only
# status: draft
# task: TASK-1
# created_date: '2026-11-06'
# updated_date: '2026-11-06'
# ---
"""Reproduce "communication with agent failed" from Secretive, and check that
receiver-side reassembly removes it.

All steps use the real ssh-keygen and a real Secretive key:
  1. direct             ssh-keygen -> Secretive
  2. split              ssh-keygen -> relay that sends each frame's 4-byte length
                        and its body as two writes with a gap -> Secretive
  3. split + reassemble ssh-keygen -> same split relay -> C relay that accumulates
                        bytes and forwards only complete frames, one write each
                        -> Secretive
  4. load               N concurrent signs, direct vs through the reassembly relay
  5. git commit -S      through the split relay, with and without reassembly
  6. control            ssh-keygen -> split relay -> OpenSSH's own ssh-agent

The C reassembly relay is embedded below and compiled with `cc` at run time.
It is a test harness that illustrates the technique, not a patch for Secretive.

Requirements: macOS, Python 3.8+, Xcode command line tools (cc, git, ssh-keygen),
and a Secretive key. Every sign request is one Secretive signature: a notify-only
key posts one notification per request, and a key that requires authentication
prompts every time, so use a notify-only key.

Usage:
    python3 secretive_repro.py <path to the Secretive key's .pub>
            [--delay 0.05] [--runs 5] [--concurrency 16] [--rounds 3]
"""
import argparse
import concurrent.futures
import os
import platform
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time

DEFAULT_AGENT = os.path.expanduser(
    "~/Library/Containers/com.maxgoedjen.Secretive.SecretAgent/Data/socket.ssh"
)

REASSEMBLE_C = r"""
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

#define MAX_LEN (256 * 1024)            /* same cap as OpenSSH AGENT_MAX_LEN */
static const char *agent_path;

static int write_all(int fd, const unsigned char *b, size_t n)
{
    while (n > 0) {
        ssize_t w = write(fd, b, n);
        if (w < 0) { if (errno == EINTR) continue; return -1; }
        b += w; n -= (size_t)w;
    }
    return 0;
}

struct pair { int client, agent; };

static void *agent_to_client(void *p)   /* replies are copied unchanged */
{
    struct pair *x = p;
    unsigned char buf[65536];
    ssize_t r;
    while ((r = read(x->agent, buf, sizeof(buf))) > 0)
        if (write_all(x->client, buf, (size_t)r) < 0) break;
    shutdown(x->client, SHUT_WR);
    return NULL;
}

/* accumulate / extract / keep the remainder; each complete frame -> one write */
static void client_to_agent(int fd, int agent)
{
    static __thread unsigned char acc[4 + MAX_LEN];
    size_t used = 0;
    for (;;) {
        ssize_t r = read(fd, acc + used, sizeof(acc) - used);   /* 1) append */
        if (r == 0) return;
        if (r < 0) { if (errno == EINTR) continue; return; }
        used += (size_t)r;
        size_t off = 0;
        while (used - off >= 4) {                               /* 2) extract */
            uint32_t len;
            memcpy(&len, acc + off, 4);
            len = ntohl(len);
            if (len > MAX_LEN) return;                          /*    cap */
            if (used - off < 4 + (size_t)len) break;            /*    incomplete */
            if (write_all(agent, acc + off, 4 + len) < 0) return;
            off += 4 + len;
        }
        memmove(acc, acc + off, used - off);                    /* 3) keep remainder */
        used -= off;
    }
}

static void *serve(void *p)
{
    int client = (int)(intptr_t)p;
    struct sockaddr_un a;
    memset(&a, 0, sizeof(a));
    a.sun_family = AF_UNIX;
    strncpy(a.sun_path, agent_path, sizeof(a.sun_path) - 1);
    int agent = socket(AF_UNIX, SOCK_STREAM, 0);
    if (agent < 0 || connect(agent, (struct sockaddr *)&a, sizeof(a)) < 0) { close(client); return NULL; }
    struct pair x = { client, agent };
    pthread_t t;
    pthread_create(&t, NULL, agent_to_client, &x);
    client_to_agent(client, agent);
    shutdown(agent, SHUT_WR);
    pthread_join(t, NULL);
    close(agent);
    close(client);
    return NULL;
}

int main(int argc, char *argv[])
{
    if (argc != 3) { fprintf(stderr, "usage: %s <listen> <agent>\n", argv[0]); return 2; }
    signal(SIGPIPE, SIG_IGN);
    agent_path = argv[2];
    struct sockaddr_un l;
    memset(&l, 0, sizeof(l));
    l.sun_family = AF_UNIX;
    strncpy(l.sun_path, argv[1], sizeof(l.sun_path) - 1);
    unlink(argv[1]);
    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    if (s < 0 || bind(s, (struct sockaddr *)&l, sizeof(l)) < 0 || listen(s, 64) < 0) { perror("listen"); return 1; }
    for (;;) {
        int c = accept(s, NULL, NULL);
        if (c < 0) { if (errno == EINTR) continue; perror("accept"); return 1; }
        pthread_t t;
        pthread_create(&t, NULL, serve, (void *)(intptr_t)c);
        pthread_detach(t);
    }
}
"""


# --------------------------------------------------------------- split relay
def recv_exact(sock, n):
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            return None
        buf += chunk
    return buf


def shutdown(*socks):
    for s in socks:
        try:
            s.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass


def split_client_to_agent(client, agent, delay):
    """Forward whole frames, writing the length and the body separately."""
    try:
        while True:
            header = recv_exact(client, 4)
            if header is None:
                break
            body = recv_exact(client, struct.unpack(">I", header)[0])
            if body is None:
                break
            agent.sendall(header)
            time.sleep(delay)
            agent.sendall(body)
    except OSError:
        pass
    finally:
        shutdown(client, agent)


def copy_agent_to_client(agent, client):
    try:
        while True:
            data = agent.recv(65536)
            if not data:
                break
            client.sendall(data)
    except OSError:
        pass
    finally:
        shutdown(client)


def start_split_relay(listen_path, agent_path, delay):
    server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    server.bind(listen_path)
    server.listen(64)

    def serve():
        while True:
            client, _ = server.accept()
            agent = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            agent.connect(agent_path)
            threading.Thread(target=split_client_to_agent, args=(client, agent, delay), daemon=True).start()
            threading.Thread(target=copy_agent_to_client, args=(agent, client), daemon=True).start()

    threading.Thread(target=serve, daemon=True).start()
    return listen_path


# ------------------------------------------------------------------ helpers
def wait_for_socket(path, timeout=5.0):
    end = time.time() + timeout
    while time.time() < end:
        if os.path.exists(path):
            return True
        time.sleep(0.05)
    return False


def sign_once(sock_path, pubkey, workdir, n):
    msg = os.path.join(workdir, f"msg{n}")
    with open(msg, "w") as f:
        f.write("test\n")
    p = subprocess.run(
        ["ssh-keygen", "-Y", "sign", "-n", "file", "-f", pubkey, msg],
        env=dict(os.environ, SSH_AUTH_SOCK=sock_path),
        capture_output=True, text=True,
    )
    err = ""
    if p.returncode != 0:
        lines = [l for l in p.stderr.splitlines() if "agent" in l or "key" in l] or p.stderr.splitlines()
        err = lines[0] if lines else f"exit {p.returncode}"
    return p.returncode == 0, err


def report(label, result, detail=""):
    print(f"  {label:<24} {result:<11} {detail}")


def sequential(label, sock_path, pubkey, workdir, runs):
    ok, last = 0, ""
    for n in range(runs):
        good, err = sign_once(sock_path, pubkey, workdir, n)
        ok += good
        last = err or last
    report(label, f"{ok}/{runs} ok", last)


def concurrent_signs(label, sock_path, pubkey, workdir, width, rounds):
    total = failed = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=width) as pool:
        for r in range(rounds):
            results = list(pool.map(lambda i: sign_once(sock_path, pubkey, workdir, 1000 + r * width + i),
                                    range(width)))
            total += len(results)
            failed += sum(1 for good, _ in results if not good)
    report(label, f"{total - failed}/{total} ok")


def git_commit(label, sock_path, pubkey, workdir):
    repo = os.path.join(workdir, "repo")
    if not os.path.isdir(repo):
        subprocess.run(["git", "init", "-q", repo], check=True)
    p = subprocess.run(
        ["git", "-C", repo, "-c", "user.name=t", "-c", "user.email=t@example.com",
         "-c", "gpg.format=ssh", "-c", f"user.signingkey={pubkey}",
         "commit", "-q", "-S", "--allow-empty", "-m", "test"],
        env=dict(os.environ, SSH_AUTH_SOCK=sock_path),
        capture_output=True, text=True,
    )
    detail = " / ".join(l for l in p.stderr.splitlines() if "agent" in l or "fatal" in l)
    report(label, f"exit {p.returncode}", detail)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("pubkey", help="public key of a Secretive key (.pub)")
    ap.add_argument("--agent", default=DEFAULT_AGENT, help="Secretive agent socket")
    ap.add_argument("--delay", type=float, default=0.05, help="gap between length and body, seconds")
    ap.add_argument("--runs", type=int, default=5)
    ap.add_argument("--concurrency", type=int, default=16)
    ap.add_argument("--rounds", type=int, default=3)
    args = ap.parse_args()
    pubkey = os.path.abspath(os.path.expanduser(args.pubkey))

    ssh_version = subprocess.run(["ssh", "-V"], capture_output=True, text=True).stderr.strip()
    print(f"ssh: {ssh_version}")
    print(f"macOS: {platform.mac_ver()[0]} {platform.machine()}")
    print(f"gap: {args.delay}s")

    procs = []
    # short directory: sun_path is limited to 104 bytes on macOS
    with tempfile.TemporaryDirectory(prefix="sr-", dir="/tmp") as tmp:
        try:
            relay_bin = os.path.join(tmp, "reassemble")
            src = os.path.join(tmp, "reassemble.c")
            with open(src, "w") as f:
                f.write(REASSEMBLE_C)
            subprocess.run(["cc", "-O2", "-o", relay_bin, src], check=True)

            fix = os.path.join(tmp, "fix.sock")
            procs.append(subprocess.Popen([relay_bin, fix, args.agent]))
            if not wait_for_socket(fix):
                sys.exit("reassembly relay did not start")
            split_direct = start_split_relay(os.path.join(tmp, "split.sock"), args.agent, args.delay)
            split_fixed = start_split_relay(os.path.join(tmp, "split-fix.sock"), fix, args.delay)

            print("\nsequential signs")
            sequential("direct", args.agent, pubkey, tmp, args.runs)
            sequential("split", split_direct, pubkey, tmp, args.runs)
            sequential("split + reassemble", split_fixed, pubkey, tmp, args.runs)

            print(f"\nload: {args.concurrency} concurrent signs x {args.rounds} rounds, no artificial gap")
            concurrent_signs("direct", args.agent, pubkey, tmp, args.concurrency, args.rounds)
            concurrent_signs("reassemble", fix, pubkey, tmp, args.concurrency, args.rounds)

            print("\ngit commit -S")
            git_commit("split", split_direct, pubkey, tmp)
            git_commit("split + reassemble", split_fixed, pubkey, tmp)

            print("\ncontrol: OpenSSH ssh-agent, throwaway ed25519 key")
            agent_sock = os.path.join(tmp, "ssh-agent.sock")
            procs.append(subprocess.Popen(["ssh-agent", "-D", "-a", agent_sock],
                                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
            if not wait_for_socket(agent_sock):
                sys.exit("ssh-agent did not start")
            key = os.path.join(tmp, "control_key")
            subprocess.run(["ssh-keygen", "-q", "-t", "ed25519", "-N", "", "-f", key], check=True)
            subprocess.run(["ssh-add", "-q", key], env=dict(os.environ, SSH_AUTH_SOCK=agent_sock), check=True)
            split_ssh_agent = start_split_relay(os.path.join(tmp, "split-agent.sock"), agent_sock, args.delay)
            sequential("split -> ssh-agent", split_ssh_agent, key + ".pub", tmp, args.runs)
        finally:
            for p in procs:
                p.terminate()
                p.wait()


if __name__ == "__main__":
    main()
