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

// Global variables
int sockfd;
FILE *file;
pthread_mutex_t file_mutex;

// Function prototypes

// Struct definitions
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
void *handle_client(void *ptr);
void signalInterruptHandler(int signo);
int createTCPServer(int deamonize);
void cleanup(Node *head, int sockfd, FILE *file);
void writeTimeStampToFile();

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

// Implement the handle_client function
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
    }
    else
    {
        perror("Unable to get client IP address");
        close(client_sockfd);
        return NULL;
    }
    syslog(LOG_INFO, "Accepted connection from %s", client_ip);
    printf("Accepted connection from %s\n", client_ip);
    // TODO: Handle incoming data from the client
    char *buffer = NULL;
    int numbytes = 0;
    int recvbytes = 0;
    while (1)
    {
        int conErr = 0, recErr = 0;
        do
        {

            buffer = realloc(buffer, numbytes + 1024);
            // check bufffer realted error
            if (buffer == NULL)
            {
                perror("realloc");
                conErr = 1;
                break;
            }
            recvbytes = recv(client_sockfd, buffer + numbytes, 1024, 0);
            if (recvbytes == 0)
            {
                syslog(LOG_INFO, "Closed connection from %s", client_ip);
                printf("Closed connection from %s\n", client_ip);
                conErr = 1;
                break;
            }
            else if (recvbytes < 0)
            {
                syslog(LOG_ERR, "Received error");
                perror("Received error\n");
                recErr = 1;
                break;
            }
            printf("Received %d bytes\n", recvbytes);
            numbytes += recvbytes;
        } while (!memchr(buffer + numbytes - recvbytes, '\n', recvbytes));
        printf("Ended str\n");
        if (conErr || recErr)
        {
            printf("Error Panic\n");
            free(buffer);
            buffer = NULL;
            numbytes = 0; // Reset num_bytes to 0
            break;
        }
        else
        {
            buffer[numbytes] = '\0';
            printf("Setting mutex\n");
            pthread_mutex_lock(&file_mutex);
            fputs(buffer, file);
            fflush(file);
            fseek(file, 0, SEEK_SET);
            size_t bufferSize = 1024;
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

            free(writeBuf);
            printf("Freeing mem after loop\n");

            free(buffer);
            buffer = NULL;
            pthread_mutex_unlock(&file_mutex);
            numbytes = 0;
        }
        // TODO: Free allocated memory and close client socket
    }
    syslog(LOG_INFO, "Closed connection from %s", client_ip);
    close(client_sockfd);
    return NULL;
}

// Implement the signalInterruptHandler function
void signalInterruptHandler(int signo)
{
    if ((signo == SIGTERM) || (signo == SIGINT))
    {
        // remove the file
        int fStatus = remove("/var/tmp/aesdsocketdata");
        if (fStatus == 0)
        {
            printf("Successfully deleted file /var/tmp/aesdsocketdata\n");
        }
        else
        {
            printf("Unable to delete file at path /var/tmp/aesdsocketdata\n");
        }

        syslog(LOG_INFO, "Caught signal, exiting");
        cleanup(NULL, sockfd, file);
        exit(EXIT_SUCCESS);
    }
    else if (signo == SIGALRM)
    {
        writeTimeStampToFile();
        fflush(file);
        fseek(file, 0, SEEK_SET);

        alarm(10);
    }
}

// Implement the cleanup function
void cleanup(Node *head, int sockfd, FILE *file)
{
    Node *current = head;
    Node *next;
    while (current != NULL)
    {
        next = current->next;
        pthread_join(current->tid, NULL);
        free(current);
        current = next;
    }

    close(sockfd);
    fclose(file);
    closelog();
}

// Implement the createTCPServer function
int createTCPServer(int deamonize)
{
    signal(SIGINT, signalInterruptHandler);
    signal(SIGTERM, signalInterruptHandler);

    // : Initialize syslog

    file = fopen("/var/tmp/aesdsocketdata", "a+");
    if (file == NULL)
    {
        syslog(LOG_ERR, "Unable to open or create the file: %s", "er");
        perror("Unable to open or create the file");
        return -1;
    }

    // : Create a TCP socket and set socket options
    openlog("aesdsocket.c", LOG_CONS | LOG_PID, LOG_USER);

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1)
    {
        syslog(LOG_ERR, "Unable to create TCP Socket");
        perror("Unable to create TCP Socket");
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

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(9000);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("bind() failed");
        return -1;
    }

    if (listen(sockfd, 5) < 0)
    {
        perror("listen() failed");
        return -1;
    }
    if (deamonize == 1)
    {
        pid_t pid = fork();

        if (pid < 0)
        {
            syslog(LOG_ERR, "Failed to fork");
            cleanup(NULL, sockfd, file);
            exit(EXIT_FAILURE);
        }

        if (pid > 0)
        {
            syslog(LOG_INFO, "Running as a daemon");
            exit(EXIT_SUCCESS);
        }

        umask(0);

        if (setsid() < 0)
        {
            syslog(LOG_ERR, "Failed to create SID for child");
            cleanup(NULL, sockfd, file);
            exit(EXIT_FAILURE);
        }

        if (chdir("/") < 0)
        {
            syslog(LOG_ERR, "Unable to change directory to root");
            cleanup(NULL, sockfd, file);
            exit(EXIT_FAILURE);
        }

        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
    }
    signal(SIGALRM, signalInterruptHandler);
    alarm(10);
    syslog(LOG_INFO, "TCP server listening at port %d", ntohs(server_addr.sin_port));
    Node *head = NULL;
    int numThreads = 0;
    // TODO: Enter the main server loop, accept connections, create client_info_t, and spawn threads
    while (1)
    {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_sockfd = accept(sockfd, (struct sockaddr *)&client_addr, &client_len);
        if (client_sockfd < 0)
        {
            perror("accept() failed");
            return -1;
        }
        // Create a new client_info_t and add it to the linked list
        client_info_t *client = (client_info_t *)malloc(sizeof(client_info_t));
        if (client == NULL)
        {
            perror("malloc() failed");
            return -1;
        }
        client->client_sockfd = client_sockfd;
        client->client_addr = client_addr;
        Node *newNode = (Node *)malloc(sizeof(Node));
        if (newNode == NULL)
        {
            perror("malloc() failed");
            return -1;
        }
        if (pthread_create(&newNode->tid, NULL, handle_client, (void *)client) != 0)
        {
            perror("pthread_create() failed");
            return -1;
        }
        newNode->next = head;
        head = newNode;
        numThreads++;
    }
    cleanup(head, sockfd, file);
    return 0;
}

// Main function
int main(int argc, char *argv[])
{
    int deamonize = 0;
    if (argc > 1)
    {
        if (strcmp(argv[1], "-d") == 0)
        {
            deamonize = 1;
        }
    }
    if (createTCPServer(deamonize) == -1)
    {
        printf("Error in running application\n");
        perror("Error in running application\n");
    }

    return 0;
}
