#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
//获取路径最后一段（真实文件名），不填充空格
char* basename(char *path)
{
  char *p;
  //找到最后一个 '/' 之后的字符
  for(p=path+strlen(path); p >= path && *p != '/'; p--)
    ;
  p++;
  return p;
}
//在 path 目录树中找名为 name 的文件
void find(char *path, char *name)
{
  char buf[512], *p;
  int fd;
  struct dirent de;
  struct stat st;
  //open 打开 path
  if((fd = open(path, 0)) < 0){
    fprintf(2, "find: cannot open %s\n", path);
    return;
  }
  //fstat 获取类型
  if(fstat(fd, &st) < 0){
    fprintf(2, "find: cannot stat %s\n", path);
    close(fd);
    return;
  }
   //switch(st.type) 分类处理：
  switch(st.type){
  case T_FILE:
    // 当前是文件：提取最后一段文件名比较
    if(strcmp(basename(path), name) == 0)
      printf("%s\n", path);
    break;

  case T_DIR:
    // 当前是目录：遍历其下的每一项
    if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf){
      printf("find: path too long\n");
      break;
    }
    strcpy(buf, path);
    p = buf+strlen(buf);
    *p++ = '/';
    while(read(fd, &de, sizeof(de)) == sizeof(de)){
      if(de.inum == 0)
        continue;                       //空目录项，跳过
      //跳过 .和 ..，避免无限递归
      if(strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0)
        continue;
      memmove(p, de.name, DIRSIZ);       //拼接完整路径
      p[DIRSIZ] = 0;
      find(buf, name);                   //递归进入子目录/文件
    }
    break;
  }
  close(fd);
}
int main(int argc, char *argv[])
{
  if(argc != 3){
    fprintf(2, "usage: find <path> <name>\n");
    exit(1);
  }
  find(argv[1], argv[2]);
  exit(0);
}
