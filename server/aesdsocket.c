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
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <asm-generic/socket.h>

#define PORT 9000
#define FILE_PATH "/var/tmp/aesdsocketdata"

volatile sig_atomic_t keep_running = 1;
int server_fd = -1, client_fd = -1;
FILE *file = NULL;
char *buffer = NULL;
size_t buffer_size = 0;

void cleanup()
{
    if (client_fd >= 0)
    {
        close(client_fd);
        client_fd = -1;
    }
    if (server_fd >= 0)
    {
        close(server_fd);
        server_fd = -1;
    }
    if (file)
    {
        fclose(file);
        file = NULL;
    }
    if (buffer)
    {
        free(buffer);
        buffer = NULL;
    }
    unlink(FILE_PATH);
    closelog();
}

void exit_with_cleanup()
{
    cleanup();
    exit(EXIT_FAILURE);
}

void my_handler(int sig)
{
    if (sig == SIGINT || sig == SIGTERM)
    {
        syslog(LOG_INFO, "Caught signal, exiting");
        keep_running = 0;
        cleanup();
        exit(EXIT_SUCCESS);
    }
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

    for (int i = 0; i < argc; i++)
    {
        if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--daemon") == 0)
        {
            daemon_mode = 1;
            break;
        }
    }

    struct sockaddr_in address;
    int opt_socket = 1;

    openlog("aesdsocket", LOG_PID, LOG_USER);

    signal(SIGINT, my_handler);
    signal(SIGTERM, my_handler);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0)
    {
        syslog(LOG_ERR, "Failed to create socket");
        exit_with_cleanup();
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt_socket, sizeof(opt_socket)) < 0)
    {
        syslog(LOG_ERR, "Failed to set socket options");
        exit_with_cleanup();
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        syslog(LOG_ERR, "Failed to bind socket");
        exit_with_cleanup();
    }

    if (listen(server_fd, 3) < 0)
    {
        syslog(LOG_ERR, "Failed to listen for connections");
        exit_with_cleanup();
    }
    if (daemon_mode)
    {
        pid_t pid = fork();

        if (pid < 0)
        {
            printf("failed to fork\n");
            close(server_fd);
            fclose(file);
            closelog();
            exit(EXIT_FAILURE);
        }

        if (pid > 0)
        {
            printf("Running as a deamon\n");
            exit(EXIT_SUCCESS);
        }

        umask(0);

        if (setsid() < 0)
        {
            printf("Failed to create SID for child\n");
            close(server_fd);
            fclose(file);
            closelog();
            exit(EXIT_FAILURE);
        }

        if (chdir("/") < 0)
        {
            printf("Unable to change directory to root\n");
            close(server_fd);
            fclose(file);
            closelog();
            exit(EXIT_FAILURE);
        }

        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
    }
    syslog(LOG_INFO, "TCP server listening at port %d", ntohs(address.sin_port));

    while (keep_running)
    {
        struct sockaddr_in client_address;
        ssize_t clientLength = sizeof(client_address);
        client_fd = accept(server_fd, (struct sockaddr *)&client_address, (socklen_t *)&clientLength);
        if (client_fd < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            syslog(LOG_ERR, "Unable to accept the client's connection: %s", strerror(errno));
            perror("Unable to accept the client's connection");
            break;
        }

        char client_ip[INET_ADDRSTRLEN];
        if (inet_ntop(AF_INET, &client_address.sin_addr, client_ip, INET_ADDRSTRLEN) == NULL)
        {
            syslog(LOG_ERR, "Failed to get client IP address");
            close(client_fd);
            continue;
        }
        syslog(LOG_INFO, "Accepted connection from %s", client_ip);
        int num_bytes = 0;

        while (keep_running)
        {
            size_t bytes_read;

            buffer = realloc(buffer, num_bytes + 4096);
            if (!buffer)
            {
                syslog(LOG_ERR, "Failed to allocate memory");
                exit_with_cleanup();
            }

            bytes_read = recv(client_fd, buffer + num_bytes, 4096, 0);
            if (bytes_read <= 0)
            {
                if (bytes_read == 0)
                {
                    syslog(LOG_INFO, "Client disconnected");
                }
                else if (errno != EINTR)
                {
                    syslog(LOG_ERR, "recv() failed: %s", strerror(errno));
                }
                break;
            }

            num_bytes += bytes_read;

            // Write received data to file immediately
            file = fopen(FILE_PATH, "a+");
            if (!file)
            {
                syslog(LOG_ERR, "Failed to open file");
                close(client_fd);
                continue;
            }
            fwrite(buffer + num_bytes - bytes_read, 1, bytes_read, file);
            fclose(file);
            file = NULL;

            // Check if we've received a complete line
            if (memchr(buffer + num_bytes - bytes_read, '\n', bytes_read) != NULL)
            {
                // Now send the entire file back to the client
                file = fopen(FILE_PATH, "r");
                if (!file)
                {
                    syslog(LOG_ERR, "Failed to open file for reading");
                    close(client_fd);
                    continue;
                }

                char send_buffer[4096];
                size_t bytes_sent;
                while ((bytes_sent = fread(send_buffer, 1, sizeof(send_buffer), file)) > 0)
                {
                    if (send(client_fd, send_buffer, bytes_sent, 0) == -1)
                    {
                        if (errno != EINTR)
                        {
                            syslog(LOG_ERR, "Failed to send data to client: %s", strerror(errno));
                            break;
                        }
                    }
                }

                fclose(file);
                file = NULL;

                // Reset buffer for next message
                num_bytes = 0;
            }
        }

        if (client_fd >= 0)
        {
            close(client_fd);
            client_fd = -1;
        }
        syslog(LOG_INFO, "Closed connection from %s", client_ip);
    }

    cleanup();
    return 0;
}