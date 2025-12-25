// iouring.c — Per-core io_uring HTTP server (2025)
// gcc -O3 -march=native -flto -pthread iouring.c -luring -o iouring
// Run with: ./iouring
// Access with: curl -v http://localhost:8080/
// wrk -c 512 -t 16 -d 15s http://localhost:8080/
// Note: Requires Linux 5.10+ with io_uring and liburing installed.

#define _GNU_SOURCE
#include <arpa/inet.h>
#include <fcntl.h>
#include <liburing.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define PORT 8080
#define RING_ENTRIES 4096
#define MAX_CONN 4096
#define BUF_SIZE 1024

#define OP_ACCEPT 1
#define OP_READ 2
#define OP_WRITE 3

#define PACK(op, ptr) ((((uint64_t)(op)) << 48) | (uint64_t)(uintptr_t)(ptr))
#define OP(x) ((int)((x) >> 48))
#define PTR(x) ((void *)(uintptr_t)((x) & 0x0000FFFFFFFFFFFFULL))

static const char RESP[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Length: 2\r\n"
    "Connection: keep-alive\r\n"
    "\r\nOK";

typedef struct
{
    int fd;
    char buf[BUF_SIZE];
} conn_t;

typedef struct
{
    int cpu;
    pthread_t tid;
    struct io_uring ring;
    int listen_fd;

    conn_t conns[MAX_CONN];
    int free_stack[MAX_CONN];
    int free_top;
} worker_t;

/* ================= Pool ================= */

static inline void pool_init(worker_t *w)
{
    w->free_top = 0;
    for (int i = 0; i < MAX_CONN; i++)
        w->free_stack[w->free_top++] = i;
}

static inline conn_t *conn_acquire(worker_t *w, int fd)
{
    if (!w->free_top)
        return NULL;
    conn_t *c = &w->conns[w->free_stack[--w->free_top]];
    c->fd = fd;
    return c;
}

static inline void conn_release(worker_t *w, conn_t *c)
{
    close(c->fd);
    w->free_stack[w->free_top++] = (int)(c - w->conns);
}

/* ================= io_uring ops ================= */

static inline void prep_accept(struct io_uring *r, int fd)
{
    struct io_uring_sqe *sqe = io_uring_get_sqe(r);
    io_uring_prep_multishot_accept(sqe, fd, NULL, NULL, SOCK_NONBLOCK);
    io_uring_sqe_set_data64(sqe, PACK(OP_ACCEPT, 0));
}

static inline void prep_read(struct io_uring *r, conn_t *c)
{
    struct io_uring_sqe *sqe = io_uring_get_sqe(r);
    io_uring_prep_recv(sqe, c->fd, c->buf, BUF_SIZE, 0);
    io_uring_sqe_set_data64(sqe, PACK(OP_READ, c));
}

static inline void prep_write(struct io_uring *r, conn_t *c)
{
    struct io_uring_sqe *sqe = io_uring_get_sqe(r);
    io_uring_prep_send(sqe, c->fd, RESP, sizeof(RESP) - 1, 0);
    io_uring_sqe_set_data64(sqe, PACK(OP_WRITE, c));
}

/* ================= Worker ================= */

static void *worker_main(void *arg)
{
    worker_t *w = arg;

    /* CPU affinity */
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(w->cpu, &set);
    pthread_setaffinity_np(pthread_self(), sizeof(set), &set);

    /* Listener */
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(PORT),
        .sin_addr.s_addr = INADDR_ANY,
    };
    bind(fd, (void *)&addr, sizeof(addr));
    listen(fd, 65535);
    w->listen_fd = fd;

    /* io_uring */
    struct io_uring_params p = {0};
    p.flags = IORING_SETUP_SQPOLL | IORING_SETUP_SINGLE_ISSUER;
    p.sq_thread_idle = 1000;

    io_uring_queue_init_params(RING_ENTRIES, &w->ring, &p);

    pool_init(w);

    prep_accept(&w->ring, w->listen_fd);
    io_uring_submit(&w->ring);

    while (1)
    {
        struct io_uring_cqe *cqe;
        io_uring_wait_cqe(&w->ring, &cqe);

        unsigned head, count = 0;
        io_uring_for_each_cqe(&w->ring, head, cqe)
        {
            count++;
            int res = cqe->res;
            uint64_t d = io_uring_cqe_get_data64(cqe);

            switch (OP(d))
            {
            case OP_ACCEPT:
                if (res >= 0)
                {
                    conn_t *c = conn_acquire(w, res);
                    if (c)
                        prep_read(&w->ring, c);
                    else
                        close(res);
                }
                if (!(cqe->flags & IORING_CQE_F_MORE))
                    prep_accept(&w->ring, w->listen_fd);
                break;

            case OP_READ:
            {
                conn_t *c = PTR(d);
                if (res <= 0)
                    conn_release(w, c);
                else
                    prep_write(&w->ring, c);
                break;
            }
            case OP_WRITE:
            {
                conn_t *c = PTR(d);
                if (res < 0)
                    conn_release(w, c);
                else
                    prep_read(&w->ring, c);
                break;
            }
            }
        }
        io_uring_cq_advance(&w->ring, count);
        io_uring_submit(&w->ring);
    }
    return NULL;
}

/* ================= Main ================= */

int main(void)
{
    int ncpu = sysconf(_SC_NPROCESSORS_ONLN);
    worker_t *workers = calloc(ncpu, sizeof(worker_t));

    for (int i = 0; i < ncpu; i++)
    {
        workers[i].cpu = i;
        pthread_create(&workers[i].tid, NULL, worker_main, &workers[i]);
    }

    for (int i = 0; i < ncpu; i++)
        pthread_join(workers[i].tid, NULL);

    return 0;
}
