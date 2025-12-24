#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/tcp.h>
#include <liburing.h>
#include <sys/mman.h>

#define max_connection_size 1024
#define max_thread_pool_size 16 // Not used in single-thread io_uring version, but kept for consistency
#define BUFFER_GROUP 0
#define max_buffers max_connection_size
#define buffer_size 4096

// user_data: high 16 bits = op, low 48 bits = fd
#define OP_ACCEPT 1
#define OP_READ 2
#define OP_WRITE 3
#define PACK(op, fd) ((((uint64_t)(op)) << 48) | (uint32_t)(fd))
#define UNPACK_OP(x) ((int)((x) >> 48))
#define UNPACK_FD(x) ((int)((uint32_t)(x)))

static const char response[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/plain\r\n"
    "Content-Length: 2\r\n"
    "Connection: keep-alive\r\n"
    "\r\n"
    "OK";

static char request_buffers[max_buffers][buffer_size];
static struct io_uring_buf_ring *buf_ring;

typedef struct
{
    int port;
    int socket_fd;
    pthread_t threads[max_thread_pool_size]; // Unused in single-thread io_uring version
    void *(*request_handler)(void *);        // Unused in this minimal version
} Server;

static void prepare_accept(struct io_uring *ring, int server_socket)
{
    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    if (!sqe)
        return;
    io_uring_prep_multishot_accept(sqe, server_socket, NULL, NULL, SOCK_NONBLOCK);
    io_uring_sqe_set_data64(sqe, PACK(OP_ACCEPT, 0));
}

static void prepare_read(struct io_uring *ring, int client_socket)
{
    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    if (!sqe)
        return;
    io_uring_prep_recv(sqe, client_socket, NULL, buffer_size, 0);
    sqe->flags |= IOSQE_BUFFER_SELECT;
    sqe->buf_group = BUFFER_GROUP;
    io_uring_sqe_set_data64(sqe, PACK(OP_READ, client_socket));
}

static void prepare_write(struct io_uring *ring, int client_socket)
{
    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    if (!sqe)
        return;
    io_uring_prep_send(sqe, client_socket, response, sizeof(response) - 1, 0);
    io_uring_sqe_set_data64(sqe, PACK(OP_WRITE, client_socket));
}

static void close_socket(int fd)
{
    close(fd);
}

static int create_server_socket(int port)
{
    int server_socket = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (server_socket < 0)
        return -1;

    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr.s_addr = INADDR_ANY,
    };
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
        goto fail;
    if (listen(server_socket, max_connection_size) < 0)
        goto fail;

    int flags = fcntl(server_socket, F_GETFL, 0);
    fcntl(server_socket, F_SETFL, flags | O_NONBLOCK);
    return server_socket;
fail:
    close_socket(server_socket);
    return -1;
}
static void server_run(Server *server)
{
    printf("listening on http://localhost:%d/\n", server->port);

    server->socket_fd = create_server_socket(server->port);
    if (server->socket_fd < 0)
    {
        perror("create_server_socket");
        exit(1);
    }

    struct io_uring ring;
    struct io_uring_params p = {0};
    p.flags = IORING_SETUP_COOP_TASKRUN | IORING_SETUP_SINGLE_ISSUER | IORING_SETUP_DEFER_TASKRUN;

    if (io_uring_queue_init_params(max_connection_size, &ring, &p) < 0)
    {
        perror("io_uring_queue_init_params");
        close_socket(server->socket_fd);
        exit(1);
    }

    // Setup buffer ring
    size_t buf_ring_size = sizeof(struct io_uring_buf_ring) + max_buffers * sizeof(struct io_uring_buf);

    buf_ring = mmap(NULL, buf_ring_size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
    if (buf_ring == MAP_FAILED)
    {
        perror("mmap");
        io_uring_queue_exit(&ring);
        close_socket(server->socket_fd);
        exit(1);
    }
    io_uring_buf_ring_init(buf_ring);

    struct io_uring_buf_reg reg = {
        .ring_addr = (unsigned long)buf_ring,
        .ring_entries = max_buffers,
        .bgid = BUFFER_GROUP};
    if (io_uring_register_buf_ring(&ring, &reg, 0) < 0)
    {
        perror("io_uring_register_buf_ring");
        munmap(buf_ring, buf_ring_size);
        io_uring_queue_exit(&ring);
        close_socket(server->socket_fd);
        exit(1);
    }

    int buf_ring_mask = io_uring_buf_ring_mask(max_buffers);

    for (int i = 0; i < max_buffers; i++)
    {
        io_uring_buf_ring_add(buf_ring, request_buffers[i], buffer_size, i, buf_ring_mask, i);
    }
    io_uring_buf_ring_advance(buf_ring, max_buffers);

    prepare_accept(&ring, server->socket_fd);
    io_uring_submit(&ring);

    while (1)
    {
        struct io_uring_cqe *cqe;
        int ret = io_uring_wait_cqe(&ring, &cqe);
        if (ret == -EINTR)
            continue;
        if (ret < 0)
            break;

        unsigned head, count = 0;
        io_uring_for_each_cqe(&ring, head, cqe)
        {
            count++;
            uint64_t data = io_uring_cqe_get_data64(cqe);
            int op = UNPACK_OP(data);
            int fd = UNPACK_FD(data);
            int res = cqe->res;

            switch (op)
            {
            case OP_ACCEPT:
                if (res >= 0)
                {
                    int client_socket = res;
                    prepare_read(&ring, client_socket);
                }
                if (!(cqe->flags & IORING_CQE_F_MORE))
                    prepare_accept(&ring, server->socket_fd);
                break;

            case OP_READ:
                if (res > 0)
                {
                    prepare_write(&ring, fd);
                }
                else
                {
                    close_socket(fd);
                }
                if (cqe->flags & IORING_CQE_F_BUFFER)
                {
                    uint16_t bid = cqe->flags >> IORING_CQE_BUFFER_SHIFT;
                    io_uring_buf_ring_add(buf_ring, request_buffers[bid], buffer_size, bid, buf_ring_mask, 0);
                    io_uring_buf_ring_advance(buf_ring, 1);
                }
                break;

            case OP_WRITE:
                if (res >= 0)
                {
                    prepare_read(&ring, fd);
                }
                else
                {
                    close_socket(fd);
                }
                break;
            }
        }
        if (count)
            io_uring_cq_advance(&ring, count);
        io_uring_submit(&ring);
    }

    // Drain remaining completions
    struct io_uring_cqe *cqe;
    while (!io_uring_peek_cqe(&ring, &cqe))
    {
        uint64_t data = io_uring_cqe_get_data64(cqe);
        int op = UNPACK_OP(data);
        int fd = UNPACK_FD(data);
        if (op == OP_READ || op == OP_WRITE)
            close_socket(fd);
        if (op == OP_READ && (cqe->flags & IORING_CQE_F_BUFFER))
        {
            uint16_t bid = cqe->flags >> IORING_CQE_BUFFER_SHIFT;
            io_uring_buf_ring_add(buf_ring, request_buffers[bid], buffer_size, bid, io_uring_buf_ring_mask(max_buffers), 0);
            io_uring_buf_ring_advance(buf_ring, 1);
        }
        io_uring_cqe_seen(&ring, cqe);
    }

    io_uring_unregister_buf_ring(&ring, BUFFER_GROUP);
    munmap(buf_ring, buf_ring_size);
    io_uring_queue_exit(&ring);
    close_socket(server->socket_fd);
    puts("Server stopped.");
}

int main(void)
{
    Server server = {
        .port = 8080,
        .socket_fd = -1,
        .request_handler = NULL // Not used in this io_uring version
    };
    server_run(&server);
    return 0;
}