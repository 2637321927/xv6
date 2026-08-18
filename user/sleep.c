#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
    if (argc <2) {
        fprintf(2, "sleep: No argument!\n"); 
        exit(1);
    }
    int ticks = atoi(argv[1]);
    if (ticks < 0) {
        fprintf(2, "sleep: invalid time %d\n", ticks);
        exit(1);
    }
    sleep(ticks);
    exit(0);
}