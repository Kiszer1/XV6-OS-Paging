#include "ustack.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"

struct buffer {
    struct buffer *prevb;
    int size;
};

typedef struct buffer Buffer;

static Buffer base;
static Buffer *lastb;

static uint64 newpage(void) {
    char *p;
    if ((p = sbrk(PGSIZE)) == (char*)-1)
        return 0;
    base.size += PGSIZE;
    return (uint64)p;
}

static Buffer* newbuffer(uint len) {
    Buffer *b;
    char *pos;
    pos = (char*)lastb + lastb->size + sizeof(Buffer);
    b = (Buffer*)(pos);
    b->size = len;
    base.size -= (len + sizeof(Buffer));
    b->prevb = lastb;
    lastb = b;
    return b + 1;
}

void* ustack_malloc(uint len) {
    if (len > MAXSIZE) {
        return (void*)-1;
    }

    Buffer *b;
    if ((b = lastb) == 0) {
        base.prevb = lastb = b = &base;
        base.size = 0;  
    }
    int newsz = base.size - sizeof(Buffer) - len;
    if (newsz < 0) {
        if ((b = (Buffer*)newpage()) == 0)
            return (void*)-1;
        if (lastb == &base) {
            b->prevb = lastb;
            b->size = len;
            lastb = b;
            base.size -= sizeof(Buffer) + len;
            return (void *) (b + 1);
        }
    }
    return (void *)newbuffer(len);
}

int ustack_free(void) {
    if (lastb == &base) {
        return -1;
    }
    base.size += lastb->size + sizeof(Buffer);
    lastb = lastb->prevb;
    if (base.size >= PGSIZE) {
        base.size -= PGSIZE;
        sbrk(-PGSIZE);
    }
    return 0;
}