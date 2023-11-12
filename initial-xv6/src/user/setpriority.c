#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

char buffer[1024];

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        printf("Usage: setpriority pid new_priority\n");
        exit(1);
    }

    if (atoi(argv[1]) < 0)
    {
        printf("Invalid pid value\n");
        exit(1);
    }

    if (atoi(argv[2]) < 0 || atoi(argv[2]) > 100)
    {
        printf("Invalid priority value\n");
        exit(1);
    }

    int ret = setpriority(atoi(argv[1]), atoi(argv[2]));
    if (ret < 0)
    {
        printf("setpriority failed\n");
        exit(1);
    }
    else
        printf("setpriority %d succeeded\n", ret);
    return 0;
}