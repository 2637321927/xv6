struct buf {
  uint tickstamp;       // 记录缓冲区最近使用时间，用于LRU淘汰
  int valid;   // has data been read from disk?
  int disk;    // does disk "own" buf?
  uint dev;
  uint blockno;
  struct sleeplock lock;
  uint refcnt;
  struct buf *next;
  uchar data[BSIZE];
};

