#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <syslog.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <asm-generic/socket.h>
#include <fcntl.h>
#include <sys/stat.h> // For umask

#define PORT 9000
#define FILE_PATH "/var/tmp/aesdsocketdata"

volatile sig_atomic_t keep_running = 1;

void my_handler(int sig)
{
    if (sig == SIGINT || sig == SIGTERM)
    {
        syslog(LOG_INFO, "Caught signal, exiting");
        keep_running = 0;
    }
}

void exit_with_cleanup(int server_fd)
{
    if (server_fd >= 0)
        close(server_fd);
    unlink(FILE_PATH);
    closelog();
    exit(EXIT_FAILURE);
}

void handle_client(int client_fd)
{
    char buffer[1024] = {0};
    ssize_t bytes_read;
    FILE *file = fopen(FILE_PATH, "a+");
    if (file == NULL)
    {
        syslog(LOG_ERR, "Failed to open file");
        close(client_fd);
        return;
    }

    while ((bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0)) > 0)
    {
        buffer[bytes_read] = '\0';
        fputs(buffer, file);
        if (strchr(buffer, '\n') != NULL)
        {
            break;
        }
    }

    if (bytes_read == -1)
    {
        syslog(LOG_ERR, "recv() failed");
    }

    fclose(file);
}

void send_file_contents(int client_fd)
{
    FILE *file = fopen(FILE_PATH, "r");
    if (file == NULL)
    {
        syslog(LOG_ERR, "Failed to open file");
        close(client_fd);
        return;
    }

    char buffer[1024] = {0};
    while (fgets(buffer, sizeof(buffer), file) != NULL)
    {
        if (send(client_fd, buffer, strlen(buffer), 0) == -1)
        {
            syslog(LOG_ERR, "send() failed");
            fclose(file);
            close(client_fd);
            return;
        }
    }
    fclose(file);
}

void daemonize()
{
    pid_t pid = fork();
    if (pid < 0)
    {
        exit(EXIT_FAILURE);
    }
    if (pid > 0)
    {
        exit(EXIT_SUCCESS);
    }

    if (setsid() < 0)
    {
        exit(EXIT_FAILURE);
    }

    signal(SIGCHLD, SIG_IGN);
    signal(SIGHUP, SIG_IGN);

    pid = fork();
    if (pid < 0)
    {
        exit(EXIT_FAILURE);
    }
    if (pid > 0)
    {
        exit(EXIT_SUCCESS);
    }

    umask(0);
    chdir("/");

    int x;
    for (x = sysconf(_SC_OPEN_MAX); x >= 0; x--)
    {
        close(x);
    }
}

int main(int argc, char *argv[])
{
    int daemon_mode = 0;
    int opt;

    while ((opt = getopt(argc, argv, "d")) != -1)
    {
        switch (opt)
        {
        case 'd':
            daemon_mode = 1;
            break;
        default:
            fprintf(stderr, "Usage: %s [-d]\n", argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    struct sockaddr_in address;
    int opt_socket = 1;
    int addrlen = sizeof(address);

    openlog("aesdsocket", LOG_PID, LOG_USER);

    signal(SIGINT, my_handler);
    signal(SIGTERM, my_handler);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0)
    {
        syslog(LOG_ERR, "Failed to create socket");
        exit_with_cleanup(server_fd);
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt_socket, sizeof(opt_socket)) < 0)
    {
        syslog(LOG_ERR, "Failed to set socket options");
        exit_with_cleanup(server_fd);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        syslog(LOG_ERR, "Failed to bind socket");
        exit_with_cleanup(server_fd);
    }

    if (listen(server_fd, 3) < 0)
    {
        syslog(LOG_ERR, "Failed to listen for connections");
        exit_with_cleanup(server_fd);
    }

    if (daemon_mode)
    {
        daemonize();
    }

    while (keep_running)
    {
        int client_fd = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen);
        if (client_fd < 0)
        {
            if (keep_running)
            {
                syslog(LOG_ERR, "Failed to accept connection");
            }
            continue;
        }

        char client_ip[INET_ADDRSTRLEN];
        if (inet_ntop(AF_INET, &address.sin_addr, client_ip, INET_ADDRSTRLEN) == NULL)
        {
            syslog(LOG_ERR, "Failed to get client IP address");
            close(client_fd);
            continue;
        }
        syslog(LOG_INFO, "Accepted connection from %s", client_ip);

        handle_client(client_fd);
        send_file_contents(client_fd);
        close(client_fd);
        syslog(LOG_INFO, "Closed connection from %s", client_ip);
    }

    close(server_fd);
    unlink(FILE_PATH);
    closelog();

    return 0;
}