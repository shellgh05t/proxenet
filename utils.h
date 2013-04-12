#ifndef _UTILS_H
#define _UTILS_H

#ifndef SIZE_MAX
#define SIZE_MAX ~((size_t)1)
#endif

#ifdef __GNUC__
#define UNUSED __attribute__ ((unused))
#else
#define UNUSED
#endif

#define MAX(x,y) (((x) >= (y))?(x):(y)) 
#define MIN(x,y) (((x) <= (y))?(x):(y)) 

typedef enum {
	FALSE = 0,
	TRUE  = 1
} boolean;

enum log_level {
	LOG_DEBUG = 0,
	LOG_INFO,
	LOG_WARNING,
	LOG_ERROR,
	LOG_CRITICAL
};

#define	RED	"\x1b[31;1m"
#define MAGENTA "\x1b[35;1m"
#define CYAN	"\x1b[36;1m"
#define BLUE	"\x1b[34;1m"
#define GREEN	"\x1b[32;1m"
#define WHITE	"\x1b[37;1m"
#define NORMAL	"\x1b[0m"


#ifdef DEBUG
#define GEN_FMT "in `%s'(%s:%d) "
#define __xlog(t, ...) _xlog(t, __VA_ARGS__)
#define xlog(t, _f, ...) __xlog(t, GEN_FMT _f, __FUNCTION__, __FILE__, __LINE__, __VA_ARGS__)

#else
#define xlog(t, ...) _xlog(t, __VA_ARGS__)
#endif

void _xlog(int type, const char* fmt, ...);
void* xmalloc(size_t size);
void xfree(void* ptr);
void xzero(void* buf, size_t buflen);
void* xrealloc(void* oldptr, size_t new_size);

#endif /* _UTILS_H */
