#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
int main(int argc, char *argv[])
{
    openlog("writer", 0, LOG_USER);
    // Check for correct number of arguments
    if (argc != 3)
    {
        fprintf(stderr, "Usage: %s <string> <file>\n", argv[0]);
        return EXIT_FAILURE;
    }
    FILE *f = fopen(argv[1], "w");
    if (f == NULL)
    {
        
        syslog(LOG_ERR, "Failed to open file %s", argv[2]);
        perror("fopen");
        return EXIT_FAILURE;
    }
    
    fprintf(f, "%s", argv[2]);
    syslog(LOG_INFO, "Writing %s to %s", argv[1], argv[2]);
    fclose(f);
    return EXIT_SUCCESS;
}