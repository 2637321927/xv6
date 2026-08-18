#include "kernel/types.h"
#include "user/user.h"
int main()
{
    // pipe1：父子；p1[1]父写，p1[0]子读
    int p1[2];
    // pipe2：子父；p2[1]子写，p2[0]父读
    int p2[2];
    char buf[1];
    if(pipe(p1) < 0){
        fprintf(2, "pipe1 create fail\n");
        exit(1);
    }
    if(pipe(p2) < 0){
        fprintf(2, "pipe2 create fail\n");
        exit(1);
    }
    int pid = fork();
    if(pid < 0){
        fprintf(2, "fork fail\n");
        //失败要关闭全部fd
        close(p1[0]); close(p1[1]);
        close(p2[0]); close(p2[1]);
        exit(1);
    }
    if(pid > 0){
        //父进程
        //父不需要p1读端、不需要p2写端，先关闭
        close(p1[0]);
        close(p2[1]);
        //父向子写1字节
        if(write(p1[1], "x", 1) != 1){
            fprintf(2,"parent write fail\n");
            exit(1);
        }
        close(p1[1]); //写完关闭写端
        //阻塞等待子进程回传字节
        if(read(p2[0], buf, 1) != 1){
            fprintf(2,"parent read fail\n");
            exit(1);
        }
        printf("%d: received pong\n", getpid());
        close(p2[0]);
        // 等待子进程退出，回收其资源
        wait(0);
    } else {
        //子进程
        //子不需要p1写端，不需要p2读端，先关闭
        close(p1[1]);
        close(p2[0]);
        //子读取父发来的字节
        if(read(p1[0], buf, 1) != 1){
            fprintf(2,"child read fail\n");
            exit(1);
        }
        printf("%d: received ping\n", getpid());
        close(p1[0]);
        //子把字节写回父进程
        if(write(p2[1], buf, 1) !=1){
            fprintf(2,"child write fail\n");
            exit(1);
        }
        close(p2[1]);
    }
    exit(0);
}
