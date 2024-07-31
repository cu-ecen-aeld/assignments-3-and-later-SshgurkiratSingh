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
void *handle_client(void *ptr);
void signalInterruptHandler(int signo);
int createTCPServer(int deamonize);
void cleanup(Node *head, int sockfd, FILE *file);
void writeTimeStampToFile();

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

void writeTimeStampToFile()
{

    time_t t = time(NULL);
    char str[80];
    strftime(str, sizeof(str), "%a, %d %b %Y %H:%M:%S %z", localtime(&t));
    pthread_mutex_lock(&file_mutex);
    fprintf(file, "timestamp:%s\n", str);
    pthread_mutex_unlock(&file_mutex);
}

// Implement the handle_client function
void *handle_client(void *ptr)
{
    // TODO: Cast the client_info_t pointer and extract client_sockfd and client_addr
    client_info_t *client_info = (client_info_t *)ptr;
    int client_sockfd = client_info->client_sockfd;
    struct sockaddr_in client_addr = client_info->client_addr;
    // TODO: Get the client's IP address and log it using syslog
    char str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(client_addr.sin_addr), str, INET_ADDRSTRLEN);
    syslog(LOG_INFO, "Accepted connection from %s", str);
    // TODO: Handle incoming data from the client
    char *buffer;
    int numbytes = 0;
    int recvbytes = 0;
    while (1)
    {
        int conErr = 0, recErr = 0;
        do
        {

        } while (!memchr(buffer + numbytes - recvbytes, '\n', recvbytes));
        // TODO: Reallocate buffer for incoming data and handle errors
        // TODO: Process the received data, write it to the file, and send the contents back to the client
        // TODO: Free allocated memory and close client socket
    }
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

    file = fopen("/var/tmp/aesdsocketdata", "a");
    // : Create a TCP socket and set socket options
    openlog("aesdsocket.c", LOG_CONS | LOG_PID, LOG_USER);

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        perror("Unable to create socket");
        return -1;
    }

    int opt = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        perror("setsockopt(SO_REUSEADDR) failed");
        return -1;
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
    if (deamonize)
    {
        pid_t pid = fork();
        if (pid < 0)
        {
            perror("fork() failed");
            return -1;
        }
        else if (pid > 0)
        {
            syslog(LOG_INFO, "Parent process exiting");
            exit(EXIT_SUCCESS);
        }
        umask(0);
        pid_t sid = setsid();
        if (sid < 0)
        {
            perror("setsid() failed");
            return -1;
        }
        if (chdir("/") < 0)
        {
            perror("chdir() failed");
            return -1;
        }
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
    }
    // TODO: Enter the main server loop, accept connections, create client_info_t, and spawn threads
    while (1)
    // TODO: Cleanup resources in case of loop exit
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
