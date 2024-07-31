#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <syslog.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <signal.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>

int sockfd;
FILE *file;

pthread_mutex_t file_mutex;

void *handle_client(void *ptr);
void signalInterruptHandler(int signo);
int createTCPServer(int deamonize);

typedef struct node
{
    pthread_t tid;
    struct node *next;
} Node;

typedef struct
{
    int client_sockfd;
    struct sockaddr_in client_addr;
} client_info_t;

// Declare the cleanup function
void cleanup(Node *head, int sockfd, FILE *file);
void writeTimeStampToFile()
{
    // get current time in a variable
    time_t t = time(NULL);
    char str[80];
    // convert time to rfc2822 format
    strftime(str, sizeof(str), "%a, %d %b %Y %H:%M:%S %z", localtime(&t));
    // printf("Current time: %s\n", str);
    // lock the mutex
    pthread_mutex_lock(&file_mutex);
    fprintf(file, "timestamp:%s\n", str);

    // unlock the mutex
    pthread_mutex_unlock(&file_mutex);
}

void *handle_client(void *ptr)
{
    client_info_t *client_info = (client_info_t *)ptr;
    int client_sockfd = client_info->client_sockfd;
    struct sockaddr_in client_addr = client_info->client_addr;
    free(client_info);

    socklen_t client_len = sizeof(client_addr);

    char client_ip[INET6_ADDRSTRLEN];

    if (getpeername(client_sockfd, (struct sockaddr *)&client_addr, &client_len) == 0)
    {
        if (client_addr.sin_family == AF_INET)
        {
            struct sockaddr_in *ipv4 = (struct sockaddr_in *)&client_addr;
            inet_ntop(AF_INET, &(ipv4->sin_addr), client_ip, INET6_ADDRSTRLEN);
        }
        else if (client_addr.sin_family == AF_INET6)
        {
            struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)&client_addr;
            inet_ntop(AF_INET6, &(ipv6->sin6_addr), client_ip, INET6_ADDRSTRLEN);
        }

        // printf("Client IP Address: %s\n", client_ip);
    }
    else
    {
        perror("Unable to get client IP address");
        close(client_sockfd);
        return NULL;
    }

    // printf("Accepted connection from %s\n", client_ip);
    syslog(LOG_INFO, "Accepted connection from %s", client_ip);

    char *buffer = NULL;
    ssize_t num_bytes = 0;
    ssize_t recv_bytes = 0;

    while (1)
    {
        int connection_closed = 0;
        int received_error = 0;

        do
        {
            buffer = realloc(buffer, num_bytes + 1024);
            if (!buffer)
            {
                syslog(LOG_INFO, "Unable to allocate space on heap");
                printf("Unable to allocate space on heap\n");
                close(sockfd);
                return NULL;
            }

            recv_bytes = recv(client_sockfd, buffer + num_bytes, 1024, 0);

            if (recv_bytes == 0)
            {
                syslog(LOG_INFO, "Closed connection from %s", client_ip);
                // printf("Closed connection from %s\n", client_ip);
                connection_closed = 1;
                break;
            }
            else if (recv_bytes < 0)
            {
                syslog(LOG_ERR, "Received error");
                perror("Received error\n");
                received_error = 1;
                break;
            }

            num_bytes += recv_bytes;
        } while (!memchr(buffer + num_bytes - recv_bytes, '\n', recv_bytes));

        if (received_error == 1 || connection_closed == 1)
        {
            free(buffer);
            buffer = NULL;
            num_bytes = 0; // Reset num_bytes to 0
            break;
        }
        else
        {
            buffer[num_bytes] = '\0';

            // printf("Packet Received %s\n", buffer);
            // printf("%s", buffer);

            pthread_mutex_lock(&file_mutex);
            fputs(buffer, file);
            fflush(file);
            fseek(file, 0, SEEK_SET);

            size_t bufferSize = 1024; // or any other size you want
            char *writeBuf = malloc(bufferSize * sizeof(char));
            if (writeBuf == NULL)
            {
                perror("Unable to allocate memory for writeBuf");
                pthread_mutex_unlock(&file_mutex);
                return NULL;
            }

            while (fgets(writeBuf, bufferSize, file) != NULL)
            {
                send(client_sockfd, writeBuf, strlen(writeBuf), 0);
            }

            free(writeBuf); // don't forget to free the memory when you're done with it

            pthread_mutex_unlock(&file_mutex);

            free(buffer);
            buffer = NULL;
            num_bytes = 0; // Reset num_bytes to 0
        }
    }

    syslog(LOG_INFO, "Closed connection from %s", client_ip);
    close(client_sockfd);
    return NULL;
}

void signalInterruptHandler(int signo)
{
    if ((signo == SIGTERM) || (signo == SIGINT))
    {
        int Status;

        Status = remove("/var/tmp/aesdsocketdata");
        if (Status == 0)
        {
            printf("Successfully deleted file /var/tmp/aesdsocket\n");
        }
        else
        {
            printf("Unable to delete file at path /var/tmp/aesdsocket\n");
        }

        printf("Gracefully handling SIGTERM\n");
        syslog(LOG_INFO, "Caught signal, exiting");
        cleanup(NULL, sockfd, file);
        exit(EXIT_SUCCESS);
    }

    if (signo == SIGALRM)
    {
        writeTimeStampToFile();
        fflush(file);
        fseek(file, 0, SEEK_SET);

        alarm(10);
    }
}

void cleanup(Node *head, int sockfd, FILE *file)
{

    // Join all threads and free Node list
    Node *current = head;
    Node *next;
    while (current != NULL)
    {
        next = current->next;
        pthread_join(current->tid, NULL);
        free(current);
        current = next;
    }

    // Close the socket and file, and cleanup syslog
    close(sockfd);
    fclose(file);
    closelog();
}

int createTCPServer(int deamonize)
{
    signal(SIGINT, signalInterruptHandler);
    signal(SIGTERM, signalInterruptHandler);

    const char *filepath = "/var/tmp/aesdsocketdata";

    file = fopen(filepath, "a+");
    if (file == NULL)
    {
        perror("Unable to open or create the file");
        return -1;
    }

    openlog("aesdsocket.c", LOG_CONS | LOG_PID, LOG_USER);

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1)
    {
        syslog(LOG_ERR, "Unable to create TCP Socket");
        perror("Unable to create TCP Socket\n");
        fclose(file);
        closelog();
        return -1;
    }

    int enable = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0)
    {
        syslog(LOG_ERR, "setsockopt(SO_REUSEADDR) failed");
        perror("setsockopt(SO_REUSEADDR) failed");
    }

    struct sockaddr_in addr;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(9000);

    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) == -1)
    {
        syslog(LOG_ERR, "TCP Socket bind failure");
        perror("TCP Socket bind failure\n");
        close(sockfd);
        fclose(file);
        closelog();
        return -1;
    }

    if (listen(sockfd, 5) == -1)
    {
        syslog(LOG_ERR, "Unable to listen at created TCP socket");
        perror("Unable to listen at created TCP socket\n");
        close(sockfd);
        fclose(file);
        closelog();
        return -1;
    }

    if (deamonize == 1)
    {
        pid_t pid = fork();

        if (pid < 0)
        {
            printf("Failed to fork\n");
            cleanup(NULL, sockfd, file);
            exit(EXIT_FAILURE);
        }

        if (pid > 0)
        {
            printf("Running as a daemon\n");
            exit(EXIT_SUCCESS);
        }

        umask(0);

        if (setsid() < 0)
        {
            printf("Failed to create SID for child\n");
            cleanup(NULL, sockfd, file);
            exit(EXIT_FAILURE);
        }

        if (chdir("/") < 0)
        {
            printf("Unable to change directory to root\n");
            cleanup(NULL, sockfd, file);
            exit(EXIT_FAILURE);
        }

        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
    }

    signal(SIGALRM, signalInterruptHandler);
    alarm(10);
    syslog(LOG_INFO, "TCP server listening at port %d", ntohs(addr.sin_port));
    // printf("TCP server listening at port %d\n", ntohs(addr.sin_port));

    int num_threads = 0;
    Node *head = NULL;

    while (1)
    {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_sockfd = accept(sockfd, (struct sockaddr *)&client_addr, &client_len);
        if (client_sockfd == -1)
        {
            syslog(LOG_ERR, "Unable to accept the client's connection");
            perror("Unable to accept the client's connection\n");
            cleanup(head, sockfd, file);
            return -1;
        }

        client_info_t *client_info = malloc(sizeof(client_info_t));
        if (client_info == NULL)
        {
            syslog(LOG_ERR, "Unable to allocate memory for client_info");
            perror("Unable to allocate memory for client_info");
            close(client_sockfd);
            continue;
        }

        client_info->client_sockfd = client_sockfd;
        client_info->client_addr = client_addr;

        Node *n = malloc(sizeof(Node));
        if (n == NULL)
        {
            syslog(LOG_ERR, "Failed to allocate memory for thread");
            perror("Failed to allocate memory for thread\n");
            close(client_sockfd);
            free(client_info);
            continue;
        }

        if (pthread_create(&(n->tid), NULL, handle_client, (void *)client_info) != 0)
        {
            syslog(LOG_ERR, "Unable to create thread");
            perror("Unable to create thread");
            close(client_sockfd);
            free(client_info);
            continue;
        }

        n->next = head;
        head = n;

        num_threads++;
    }

    // Cleanup in case the server loop exits (shouldn't happen in this case)
    cleanup(head, sockfd, file);
    return 0;
}

int main(int argc, char *argv[])
{
    int deamonize = 0;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-d") == 0)
        {
            deamonize = 1;
        }
    }

    if (createTCPServer(deamonize) == -1)
    {
        printf("Error in running application\n");
    }

    return 0;
}