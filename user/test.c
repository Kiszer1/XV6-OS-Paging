#include "../kernel/types.h"
#include "../kernel/stat.h"
#include "user.h"


int
main(int argc, char *argv[])
{
    char* thingy;
    thingy = malloc(4096);
    int pid;
    if ((pid = fork()) != 0) {
        wait(&pid);
        printf("done waiting\n");
    }
    gets(thingy, 10);
    printf("%s", thingy);
    exit(0);
}