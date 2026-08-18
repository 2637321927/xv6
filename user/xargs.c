#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/param.h"
#include "user/user.h"

#define BUFSZ 512

// 读取一行，读到'\n'或者EOF；返回>0读到有效字符，返回0代表EOF
static int readline(char *buf, int maxsz)
{
    int pos = 0;
    char c;
    int n;
    while(pos < maxsz - 1){
        n = read(0, &c, 1);
        if(n < 0){
            fprintf(2, "xargs: read error\n");
            exit(1);
        }
        if(n == 0){
            break;
        }
        if(c == '\n'){
            break;
        }
        buf[pos++] = c;
    }
    buf[pos] = '\0';
    // EOF且没有读到任何字符
    if(n == 0 && pos == 0){
        return 0;
    }
    return pos;
}

int
main(int argc, char **argv)
{
    char buf[BUFSZ];
    char *new_argv[MAXARG];

    // 复制原始命令参数到 new_argv
    int i;
    for (i = 0; i < argc; i++) {
        new_argv[i] = argv[i];
    }

    // 循环：每次读取一行输入，一行执行一次命令
    while(1){
        int ret = readline(buf, BUFSZ);
        if(ret == 0){
            break;
        }

        // 每一行都从原始argc位置开始追加本行解析出来的单词
        int argv_len = argc;
        char *p = buf;

        for(;;){
            // 跳过连续空格，原地把空格替换为'\0'
            while (*p && *p == ' ') {
                *p++ = '\0';
            }
            // 如果已经到字符串末尾，直接退出解析循环
            if(*p == '\0'){
                break;
            }
            if (argv_len >= MAXARG - 1) {
                fprintf(2, "Too many arguments\n");
                exit(1);
            }
            new_argv[argv_len++] = p;
            // 走到下一个空格或者字符串末尾
            while (*p && *p != ' ') {
                p++;
            }
        }
        new_argv[argv_len] = 0;

        int pid = fork();
        if (pid < 0) {
            fprintf(2, "xargs: fork failed\n");
            exit(1);
        }

        if (pid > 0) {
            // 父进程等待当前行的子进程执行完成
            wait(0);
        } else {
            // 子进程执行，不要close(0)
            exec(new_argv[1], new_argv + 1);
            fprintf(2, "xargs: exec %s failed\n", new_argv[1]);
            exit(1);
        }
    }

    exit(0);
}
