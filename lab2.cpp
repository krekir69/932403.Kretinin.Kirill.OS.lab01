#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static volatile sig_atomic_t was_sighup = 0;

static void sighup_handler(int signal_number)
{
    was_sighup = 1;
}
static int create_socket_and_bind(int port)
{
    struct sockaddr_in addr;
    int fd, opt = 1;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) { perror("socket"); return -1; }
    if (fd >= FD_SETSIZE) { fprintf(stderr, "fd вне fd_set\n"); close(fd); return -1; }
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("setsockopt"); close(fd); return -1;
    }
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((unsigned short)port);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("bind"); close(fd); return -1;
    }
    if (listen(fd, 16) == -1) {
        perror("listen"); close(fd); return -1;
    }
    return fd;
}

static int reg_and_block_signal(sigset_t *original_mask)
{
    sigset_t blocked;
    struct sigaction act;

    sigemptyset(&blocked);
    sigaddset(&blocked, SIGHUP);
    if (sigprocmask(SIG_BLOCK, &blocked, original_mask) == -1) {
        perror("sigprocmask");
        return -1;
    }
    memset(&act, 0, sizeof(act));
    act.sa_handler = sighup_handler;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    if (sigaction(SIGHUP, &act, NULL) == -1) {
        perror("sigaction");
        sigprocmask(SIG_SETMASK, original_mask, NULL);
        return -1;
    }
    return 0;
}

static void on_accept(int server_fd, int *client_fd)
{
    int fd = accept(server_fd, NULL, NULL);

    if (fd == -1) {
        if (errno != EINTR && errno != ECONNABORTED) perror("accept");
        return;
    }
    if (*client_fd == -1 && fd < FD_SETSIZE) {
        *client_fd = fd;
        printf("Новое подключение принято (fd=%d)\n", fd);
    } else {
        printf("Подключение закрыто сразу (fd=%d)\n", fd);
        close(fd);
    }
    fflush(stdout);
}

static void on_client_data(int *client_fd)
{
    char buf[1024];
    ssize_t n = recv(*client_fd, buf, sizeof(buf), 0);

    if (n > 0) {
        printf("Получено байт: %zd\n", n);
        fflush(stdout);
        return;
    }
    if (n == 0) printf("Клиент закрыл соединение\n");
    else if (errno == EINTR) return;
    else perror("recv");
    fflush(stdout);
    close(*client_fd);
    *client_fd = -1;
}
static int create_connection(int server_fd, const sigset_t *original_mask)
{
    sigset_t wait_mask = *original_mask;
    int client_fd = -1, rc = 0;

    sigdelset(&wait_mask, SIGHUP); 

    for (;;) {
        fd_set rfds;
        int max_fd = server_fd, monitored = client_fd, ready;

        FD_ZERO(&rfds);
        FD_SET(server_fd, &rfds);
        if (client_fd != -1) {
            FD_SET(client_fd, &rfds);
            if (client_fd > max_fd) max_fd = client_fd;
        }

        ready = pselect(max_fd + 1, &rfds, NULL, NULL, NULL, &wait_mask);

        if (was_sighup) {
            was_sighup = 0;
            printf("Получен сигнал SIGHUP\n");
            fflush(stdout);
        }
        if (ready == -1) {
            if (errno == EINTR) continue;
            perror("pselect");
            rc = -1;
            break;
        }
        if (FD_ISSET(server_fd, &rfds)) on_accept(server_fd, &client_fd);
        if (monitored != -1 && FD_ISSET(monitored, &rfds)) on_client_data(&client_fd);
    }

    if (client_fd != -1) close(client_fd); 
    return rc;
}

int main(void)
{
    int port = 7777, server_fd = create_socket_and_bind(port);
    if (server_fd == -1) return -1;

    sigset_t original_mask;
    if (reg_and_block_signal(&original_mask) == -1)
    {
        close(server_fd);
        return -1;
    }

    printf("Сервер: порт %d, PID %ld\n", port, (long)getpid());
    fflush(stdout);

    int rc = create_connection(server_fd, &original_mask);

    close(server_fd);
    sigprocmask(SIG_SETMASK, &original_mask, NULL);

    if (rc == -1) return EXIT_FAILURE;
    return EXIT_SUCCESS;
}