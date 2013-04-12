#ifdef _PYTHON_PLUGIN
#  include <Python.h>
#endif
#include <stdarg.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include "core.h"
#include "utils.h"


/**
 *
 */
void _xlog(int type, const char* fmt, ...) 
{
	va_list ap;
	
	/* lock tty before printing */
	sem_wait(&tty_semaphore);
	
	switch (type) {
		case LOG_CRITICAL:
			if (cfg->use_color) fprintf(cfg->logfile_fd, RED);
			fprintf(cfg->logfile_fd, "CRITICAL: ");
			break;
			
		case LOG_ERROR:
			if (cfg->use_color) fprintf(cfg->logfile_fd, MAGENTA);
			fprintf(cfg->logfile_fd, "ERROR ");
			break;

		case LOG_WARNING:
			if (cfg->use_color) fprintf(cfg->logfile_fd, CYAN);
			fprintf(cfg->logfile_fd, "WARNING: ");
			break;
						    
		case LOG_DEBUG:
			if (cfg->use_color) fprintf(cfg->logfile_fd, BLUE);
			fprintf(cfg->logfile_fd, "DEBUG: ");
			break;

		case LOG_INFO:
		default:
			if (cfg->use_color) fprintf(cfg->logfile_fd, GREEN);
			fprintf(cfg->logfile_fd, "INFO: ");
			break;
	}

	if (cfg->use_color) fprintf(cfg->logfile_fd, NORMAL);

/*	
#ifdef DEBUG
	fprintf(cfg->logfile_fd, "tid-%ld ", pthread_self());
#endif
*/	
	va_start(ap, fmt);
	vfprintf(cfg->logfile_fd, fmt, ap);
	fflush(cfg->logfile_fd);
	va_end(ap);
	
	/* release lock */
	sem_post(&tty_semaphore);
}



/**
 * malloc(3) wrapper. Checks size and zero-fill buffer.
 *
 * @param size: buffer size to allocate on heap
 * @return ptr: allocated zero-filled buffer pointer
 */
void* xmalloc(size_t size)
{
	void *ptr;
	
	if (size > SIZE_MAX / sizeof(size_t)) {
		xlog(LOG_CRITICAL, "xmalloc: try to allocate incorrect size (%d byte)\n", size); 
		abort();
	}
	
	ptr = malloc(size);
	if ( ptr == NULL ) {
		xlog(LOG_CRITICAL, "%s\n", "xmalloc: fail to allocate space");
		abort();
	}
	
	xzero(ptr, size);
	return ptr;
}


/**
 * Free allocated blocks
 *
 * @param ptr: pointer to zone to free
 */
void xfree(void* ptr)
{
	if(ptr == NULL) {
		xlog(LOG_ERROR, "Trying to free null pointer at %p\n", ptr);
		abort();
	}
	
	free(ptr);
	ptr = NULL;
}


/**
 * realloc(3) wrapper. Checks size and zero-fill buffer.
 *
 * @param oldptr: pointer to previous area
 * @param new_size: new size to allocate
 * @return a pointer to resized pointer
 */
void* xrealloc(void* oldptr, size_t new_size)
{
	void *newptr;
	
	if (new_size > (SIZE_MAX / sizeof(size_t))) {
		xlog(LOG_CRITICAL, "xrealloc: try to allocate incorrect size (%d byte)\n", new_size); 
		abort();
	}
	
	newptr = realloc(oldptr, new_size);
	if (newptr == NULL) {
		xlog(LOG_CRITICAL, "%s\n", "xrealloc: fail to allocate space");
		abort();
	}
	
	return newptr;
}


/**
 * Fill buflen-sized buffer with zeroes
 *
 * @param buf : buffer to zero-ize
 * @param buflen : buf length
 */
void xzero(void* buf, size_t buflen)
{
	if (buflen)
		memset(buf, 0, buflen);
	
}
