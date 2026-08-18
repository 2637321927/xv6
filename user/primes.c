#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
// 每个素数对应一个进程，递归创建筛分子进程
void prime_sieve(int *p) {
    int prime;
    int n = read(p[0], &prime, sizeof(prime));
    if (n <= 0) {          // 读到 EOF（0）或出错（<0），无数据则结束
        close(p[0]);
        exit(0);
    }
    printf("prime %d\n", prime);

    // 创建新管道，用于向下一级筛子传数据
    int np[2];
    if (pipe(np) < 0) {
        fprintf(2, "pipe failed\n");
        exit(1);
    }

    // fork 出下一级筛子进程
    int pid = fork();
    if (pid < 0) {
        fprintf(2, "fork failed\n");
        exit(1);
    }

    if (pid == 0) {
        // 子进程：继续处理下游数据（递归）
        close(np[1]);          // 子不需要新管道写端
        close(p[0]);           // 子不需要旧管道读端
        prime_sieve(np);       // 递归：子进程成为下一级筛子
        exit(0);
    } else {
        // 父进程（当前筛子）：读取旧管道剩余数据，过滤掉 prime 的倍数
        close(np[0]);          // 父不需要新管道读端

        int tmp;
        while ((n = read(p[0], &tmp, sizeof(tmp))) > 0) {
            if (tmp % prime != 0) {
                if (write(np[1], &tmp, sizeof(tmp)) != sizeof(tmp)) {
                    fprintf(2, "write failed\n");
                    exit(1);
                }
            }
        }
        close(p[0]);           // 旧管道读完，关闭读端
        close(np[1]);          // 新管道写端关闭 → 下游才能读到 EOF

        // 等待子进程（及其所有后代）全部结束，回收资源
        wait(0);
        close(np[0]);          // 关闭新管道读端
        exit(0);
    }
}
int main(int argc, char *argv[]) {
    // 父进程将数字2~35输入管道
    int p[2];
    if (pipe(p) < 0) {
        fprintf(2, "pipe failed\n");
        exit(1);
    }
    for (int i = 2; i <= 35; i++) {
        if (write(p[1], &i, sizeof(i)) != sizeof(i)) {
            fprintf(2, "write failed\n");
            exit(1);
        }
    }
    close(p[1]);   // 主进程写完，关闭写端，下游才能读到 EOF

    // 创建筛子进程链，每个素数一个进程，及时关闭不需要的 fd
    prime_sieve(p);

    //主进程等待整个管道链终止
    close(p[0]);
    exit(0);
}
