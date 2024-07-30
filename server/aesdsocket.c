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
#include <pthread.h>
#include <time.h>

#define PORT 9000
#define FILE_PATH "/var/tmp/aesdsocketdata"

volatile sig_atomic_t keep_running = 1;
int server_fd = -1, client_fd = -1;
FILE *file = NULL;
char *buffer = NULL;
size_t buffer_size = 0;

pthread_t t_thread;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

struct thread_node
{
    pthread_t thread_id;
    int client_fd;
    struct thread_node *next;
};
struct thread_node *thread_list = NULL;
void cleanup();
void *handle_connection(void *client_socket);
void *timestamp_thread(void *arg);
void add_thread(pthread_t thread_id, int client_fd);
void remove_thread(pthread_t thread_id);
void cleanup_threads();

void writeTimeStampToFile()
{
    // get current time in a variable
    time_t t = time(NULL);
    char str[80];
    // convert time to rfc2822 format
    strftime(str, sizeof(str), "%a, %d %b %Y %H:%M:%S %z", localtime(&t));
    // printf("Current time: %s\n", str);
    // lock the mutex
    pthread_mutex_lock(&mutex);
    // open file in append mode
    file = fopen(FILE_PATH, "a");
    // write current time to file
    fprintf(file, "timestamp:%s\n", str);
    // close file
    fclose(file);
    // unlock the mutex
    pthread_mutex_unlock(&mutex);
}

void *timestamp_thread(void *arg)
{
    while (keep_running)
    {

        writeTimeStampToFile();
        sleep(10);
    }
    return NULL;
}
void cleanup()
{
    keep_running = 0;
    if (server_fd >= 0)
    {
        close(server_fd);
        server_fd = -1;
    }
    pthread_join(t_thread, NULL);
    cleanup_threads();
    pthread_mutex_destroy(&mutex);
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

void *handle_connection(void *arg) {
    int client_fd = *((int *)arg);
    free(arg);

    char buffer[1024];
    ssize_t num_bytes = 0;

    while ((num_bytes = recv(client_fd, buffer, sizeof(buffer), 0)) > 0) {
        buffer[num_bytes] = '\0';
        pthread_mutex_lock(&mutex);
        file = fopen(FILE_PATH, "a");
        if (file) {
            fwrite(buffer, 1, num_bytes, file);
            fclose(file);
        }
        pthread_mutex_unlock(&mutex);

        if (strchr(buffer, '\n') != NULL) {
            pthread_mutex_lock(&mutex);
            file = fopen(FILE_PATH, "r");
            if (file) {
                while (fgets(buffer, sizeof(buffer), file) != NULL) {
                    send(client_fd, buffer, strlen(buffer), 0);
                }
                fclose(file);
            }
            pthread_mutex_unlock(&mutex);
        }
    }
    if (num_bytes == 0) {
        syslog(LOG_INFO, "Client disconnected");
    } else if (num_bytes < 0) {
        syslog(LOG_ERR, "recv() failed: %s", strerror(errno));
    }
    close(client_fd);
    return NULL;
}

void add_thread(pthread_t thread_id, int client_fd)
{
    struct thread_node *new_node = malloc(sizeof(struct thread_node));
    new_node->thread_id = thread_id;
    new_node->client_fd = client_fd;
    new_node->next = thread_list;
    thread_list = new_node;
}

void remove_thread(pthread_t thread_id)
{
    struct thread_node **pp = &thread_list;
    while (*pp)
    {
        struct thread_node *node = *pp;
        if (pthread_equal(node->thread_id, thread_id))
        {
            *pp = node->next;
            free(node);
            return;
        }
        pp = &node->next;
    }
}
void cleanup_threads()
{
    while (thread_list)
    {
        pthread_join(thread_list->thread_id, NULL);
        close(thread_list->client_fd);
        struct thread_node *temp = thread_list;
        thread_list = thread_list->next;
        free(temp);
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
    pthread_create(&t_thread, NULL, timestamp_thread, NULL);

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
        socklen_t client_len = sizeof(client_address);

        int *client_fd = malloc(sizeof(int));
        *client_fd = accept(server_fd, (struct sockaddr *)&client_address, &client_len);

        if (*client_fd < 0)
        {
            if (errno == EINTR)
            {
                free(client_fd);
                continue;
            }
            syslog(LOG_ERR, "Failed to accept connection");
            free(client_fd);
            break;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_address.sin_addr, client_ip, INET_ADDRSTRLEN);
        syslog(LOG_INFO, "Accepted connection from %s", client_ip);

        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, handle_connection, client_fd) != 0)
        {
            syslog(LOG_ERR, "Failed to create thread");
            close(*client_fd);
            free(client_fd);
            continue;
        }

        add_thread(thread_id, *client_fd);
    }

    cleanup();
    return 0;
}