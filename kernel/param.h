#define NPROC        64  // maximum number of processes
#define NCPU          8  // maximum number of CPUs
#define NOFILE       16  // open files per process
#define NFILE       100  // open files per system
#define NINODE       50  // maximum number of active i-nodes
#define NDEV         10  // maximum major device number
#define ROOTDEV       1  // device number of file system root disk
#define MAXARG       32  // max exec arguments
#define MAXOPBLOCKS  10  // max # of blocks any FS op writes
#define LOGSIZE      (MAXOPBLOCKS*3)  // max data blocks in on-disk log
#define NBUF         (MAXOPBLOCKS*3)  // size of disk block cache
#define FSSIZE       2000  // size of file system in blocks
#define MAXPATH      128   // maximum file path name
#define MAX_PSYC_PAGES  16  // maximum number of physical pages
#define MAX_PAGED_PAGES 16  // maximum number of pages in swapfile
#define MAX_TOTAL_PAGES 32  // maximum number of pages

#define INMEMORY     1
#define PAGED        2

#define NONE         0
#define SCFIFO       1
#define NFUA         2
#define LAPA         3

