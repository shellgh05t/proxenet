#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <fnmatch.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "core.h"
#include "utils.h"
#include "socket.h"
#include "http.h"
#include "ssl.h"
#include "main.h"

#define HTTP_STRING  "http"
#define HTTPS_STRING "https"
#define HTTP_PROTO_STRING  HTTP_STRING"://"
#define HTTPS_PROTO_STRING HTTPS_STRING"://"


/**
 *
 */
static void generic_http_error_page(sock_t sock, char* msg)
{
	char* html_header = "<html><body><h1>proxenet error page</h1><br/>";
	char* html_footer = "</body></html>";

	if (write(sock, html_header, strlen(html_header)) < 0) {
		xlog(LOG_ERROR, "%s\n", "Failed to write error HTML header");
	}

	if(write(sock, msg, strlen(msg)) < 0){
		xlog(LOG_ERROR, "%s\n", "Failed to write error HTML page");
	}

	if(write(sock, html_footer, strlen(html_footer)) < 0){
		xlog(LOG_ERROR, "%s\n", "Failed to write error HTML footer");
	}
}


/**
 *
 */
static char* get_request_full_uri(request_t* req)
{
	char* uri;
	http_request_t* http_infos = &req->http_infos;
	size_t len;

	if (!req || !http_infos)
                return NULL;

	len = sizeof(HTTPS_PROTO_STRING) + strlen(http_infos->hostname) + sizeof(":") + sizeof("65535");
	len+= strlen(http_infos->path);
	uri = (char*)proxenet_xmalloc(len+1);

	snprintf(uri, len, "%s%s:%d%s",
		 req->is_ssl?HTTPS_PROTO_STRING:HTTP_PROTO_STRING,
		 http_infos->hostname,
		 http_infos->port,
		 http_infos->path);

	return uri;
}


/**
 * This function defines the request type with the hostname and port based on the URI gathered
 * from the buffer.
 *
 * This is for example used for
 * CONNECT IP:PORT HTTP/1.0
 * types of requests.
 *
 * If :PORT is not found, then req.http_infos.port is left untouched (must be defined priorly).
 *
 * @return 0 on success, -1 if error
 */
static int get_hostname_from_uri(request_t* req, int offset)
{
        char *ptr, c, *buf;

        buf = req->data + offset;

        /* isolate the whole block 'IP:PORT' */
	ptr = strchr(buf, ' ');
	if (!ptr){
                xlog(LOG_ERROR, "%s\n", "Invalid URI block");
                return -1;
        }

        c = *ptr;
	*ptr = '\0';

        buf = proxenet_xstrdup2(buf);

        /* host and port */
        ptr = strchr(buf, ':');
	if (ptr){
                /* explicit port */
                req->http_infos.port = (unsigned short)atoi(ptr+1);
                *ptr = '\0';
        }

        req->http_infos.hostname = proxenet_xstrdup2(buf);

	*ptr = c;
        proxenet_xfree(buf);

        return 0;
}


/**
 *
 */
static int get_hostname_from_header(request_t *req)
{
        char *ptr, *ptr2, c;
        const char host_header_prefix[] = "Host: ";

        /* get Host header */
        ptr = strstr(req->data, host_header_prefix);
        if(!ptr){
                xlog(LOG_ERROR, "%s\n", "No Host header found");
                return -1;
        }

        /* move to start of hostname  */
        ptr += strlen(host_header_prefix);
        ptr2 = ptr;

        /* copy hostname */
        for(; *ptr2 && *ptr2!=':' && *ptr2!=' ' && *ptr2!='\r'; ptr2++);
        c = *ptr2;
        *ptr2 = '\0';
	req->http_infos.hostname = proxenet_xstrdup2(ptr);
        if (!req->http_infos.hostname){
                xlog(LOG_ERROR, "%s\n", "strdup(hostname) failed, cannot pursue...");
                return -1;
        }

        /* if port number, copy it */
        if (c == ':'){
                req->http_infos.port = (unsigned short)atoi(ptr2+1);
        }

        *ptr2 = c;
        return 0;
}


/**
 * Modifies the HTTP request to strip out the protocol and hostname
 *
 * @return 0 if no error was encountered, -1 otherwise.
 */
int format_http_request(char** request, size_t* request_len)
{
	size_t new_request_len = 0;
	char *old_ptr, *new_ptr;
	unsigned int i;
	int offlen;

        offlen = -1;
	old_ptr = new_ptr = NULL;
	old_ptr = strstr(*request, HTTP_PROTO_STRING);
	if (old_ptr)
		offlen = 7;
	else {
		old_ptr = strstr(*request, HTTPS_PROTO_STRING);
		if (old_ptr)
			offlen = 8;
	}

	if (offlen < 0) {
		xlog(LOG_ERROR, "Cannot find protocol (%s|%s) in request:\n%s\n",
                     HTTP_STRING, HTTPS_STRING, *request);
		return -1;
	}

	new_ptr = strchr(old_ptr + offlen, '/');
	if (!new_ptr) {
		xlog(LOG_ERROR, "%s\n", "Cannot find path (must not be implicit)");
		return -1;
	}

	new_request_len = *request_len - (new_ptr-old_ptr);

#ifdef DEBUG
	xlog(LOG_DEBUG, "Formatting HTTP request (%dB->%dB)\n", *request_len, new_request_len);
#endif

	for (i=0; i<new_request_len - (old_ptr-*request);i++) {
		*(old_ptr+i) = *(new_ptr+i);
        }

	*request = proxenet_xrealloc(*request, new_request_len);
	*request_len = new_request_len;

	return 0;
}


/**
 * This function updates all the fields of the current request_t with the new values found in the
 * request. Since those values will be useful many times, they are strdup-ed in a specific structure
 * (http_request_t). Those values *must* be free-ed later on.
 *
 * @return 0 if successful, -1 if any error occurs.
 */
int update_http_infos(request_t *req)
{
	char *ptr, *buf, c;

	buf = req->data;

	/* method */
	ptr = strchr(buf, ' ');
	if (!ptr){
                xlog(LOG_ERROR, "%s\n", "Cannot find HTTP method in request");
                if (cfg->verbose)
                        xlog(LOG_ERROR, "Buffer sent:\n%s\n", buf);
                return -1;
        }

	c = *ptr;
	*ptr = '\0';
	req->http_infos.method = proxenet_xstrdup2(buf);
        if (!req->http_infos.method){
                xlog(LOG_ERROR, "%s\n", "strdup(method) failed, cannot pursue...");
                return -1;
        }

	*ptr = c;
        if (!strcmp(req->http_infos.method, "CONNECT")){
                int offset;

                req->is_ssl = true;
                req->http_infos.proto = HTTPS_STRING;
                req->http_infos.port = HTTPS_DEFAULT_PORT;
                offset = ptr - buf + 1;

                if( get_hostname_from_uri(req, offset) < 0 ){
                        xlog(LOG_ERROR, "%s\n", "Failed to get hostname (URI)");
                        goto failed_hostname;
                }

                req->http_infos.path = proxenet_xstrdup2("/");
                req->http_infos.version = proxenet_xstrdup2("HTTP/1.0");

                req->http_infos.uri = get_request_full_uri(req);
                if(!req->http_infos.uri){
                        xlog(LOG_ERROR, "%s\n", "get_request_full_uri() failed");
                        goto failed_uri;
                }

                return 0;
        }


        /* hostname and port */
        if (req->is_ssl){
                req->http_infos.port = HTTPS_DEFAULT_PORT;
                req->http_infos.proto = HTTPS_STRING;
        } else {
                req->http_infos.port = HTTP_DEFAULT_PORT;
                req->http_infos.proto = HTTP_STRING;
        }

        if( get_hostname_from_header(req) < 0 ){
                xlog(LOG_ERROR, "%s\n", "Failed to get hostname (Host header)");
                goto failed_hostname;
        }


	/* path */
        buf = ptr+1;

        if (!strncmp(buf, HTTP_PROTO_STRING, strlen(HTTP_PROTO_STRING))){
                buf = strchr(buf + 8, '/');
        }

	ptr = strchr(buf, ' ');
	if (!ptr){
                xlog(LOG_ERROR, "%s\n", "Cannot find HTTP path in request");
                goto failed_path;
        }

	c = *ptr;
	*ptr = '\0';
	req->http_infos.path = proxenet_xstrdup2(buf);
        if (!req->http_infos.path){
                xlog(LOG_ERROR, "%s\n", "strdup(path) failed, cannot pursue...");
                goto failed_path;
        }
	*ptr = c;

	buf = ptr+1;


	/* version */
	ptr = strchr(req->data, '\r');
	if (!ptr){
                xlog(LOG_ERROR, "%s\n", "Cannot find HTTP version");
                goto failed_version;
        }

	c = *ptr;
	*ptr = '\0';
	req->http_infos.version = proxenet_xstrdup2(buf);
        if (!req->http_infos.version){
                xlog(LOG_ERROR, "%s\n", "strdup(version) failed, cannot pursue...");
                goto failed_version;
        }
	*ptr = c;


        /* refresh uri */
        req->http_infos.uri = get_request_full_uri(req);
        if(!req->http_infos.uri){
                xlog(LOG_ERROR, "%s\n", "get_request_full_uri() failed");
                goto failed_uri;
        }


        if (cfg->verbose) {
                xlog(LOG_INFO, "New request %d to '%s'\n",
                     req->id, req->http_infos.uri);

                if (cfg->verbose >= 2) {
                        xlog(LOG_INFO,
                             "Request HTTP information:\n"
                             "method=%s\n"
                             "proto=%s\n"
                             "hostname=%s\n"
                             "port=%d\n"
                             "path=%s\n"
                             "version=%s\n"
                             ,
                             req->http_infos.method,
                             req->http_infos.proto,
                             req->http_infos.hostname,
                             req->http_infos.port,
                             req->http_infos.path,
                             req->http_infos.version);
                }
        }

        return 0;


failed_uri:
        proxenet_xfree(req->http_infos.version);

failed_version:
        proxenet_xfree(req->http_infos.path);

failed_path:
        proxenet_xfree(req->http_infos.hostname);

failed_hostname:
        proxenet_xfree(req->http_infos.method);

        return -1;
}


/**
 * Free all the heap allocated blocks on http_infos (allocated by update_http_infos()).
 */
void free_http_infos(http_request_t *hi)
{
        proxenet_xclean(hi->method,   strlen(hi->method));
        proxenet_xclean(hi->hostname, strlen(hi->hostname));
        proxenet_xclean(hi->path,     strlen(hi->path));
        proxenet_xclean(hi->version,  strlen(hi->version));
        proxenet_xclean(hi->uri,      strlen(hi->uri));

        hi->proto = NULL;
        hi->port = 0;
        return;
}


/**
 * Establish a connection from proxenet -> server.
 * If proxy forwarding configured, then this function performs the negociation with the other proxy.
 * If the host applies for SSL intercept rules, this function also handles the SSL handshake.
 *
 * @return 0 if successful, -1 otherwise
 */
int create_http_socket(request_t* req, sock_t* server_sock, sock_t* client_sock, ssl_context_t* ssl_ctx)
{
	int retcode;
	char *host, *port;
	char sport[6] = {0, };
	http_request_t* http_infos = &req->http_infos;
	bool use_proxy = (cfg->proxy.host != NULL) ;

        if (update_http_infos(req) < 0){
                xlog(LOG_ERROR, "%s\n", "Failed to extract valid parameters from URL.");
		return -1;
	}

	ssl_ctx->use_ssl = req->is_ssl;
	snprintf(sport, 5, "%u", http_infos->port);

	/* do we forward to another proxy ? */
	if (use_proxy) {
		host = cfg->proxy.host;
		port = cfg->proxy.port;
	} else {
		host = http_infos->hostname;
		port = sport;
	}


#ifdef DEBUG
        xlog(LOG_DEBUG, "Relay request %s to '%s:%s'\n",
             use_proxy ? "via proxy":"direct",
             host, port);
#endif

	retcode = create_connect_socket(host, port);
	if (retcode < 0) {
		if (errno)
			generic_http_error_page(*server_sock, strerror(errno));
		else
			generic_http_error_page(*server_sock, "Unknown error in <i>create_connect_socket</i>");

		return -1;
	}

#ifdef DEBUG
        xlog(LOG_DEBUG, "Socket to %s '%s:%s' : %d\n",
             use_proxy?"proxy":"server",
             host, port,
             retcode);
#endif

        *client_sock = retcode;

        req->do_intercept = ( (cfg->intercept_mode==INTERCEPT_ONLY && \
                               fnmatch(cfg->intercept_pattern, http_infos->hostname, 0)==0) || \
                              (cfg->intercept_mode==INTERCEPT_EXCEPT && \
                               fnmatch(cfg->intercept_pattern, http_infos->hostname, 0)==FNM_NOMATCH) );

#ifdef DEBUG
        xlog(LOG_DEBUG, "Server '%s' %s match interception '%s' with pattern '%s'\n",
             http_infos->hostname,
             req->do_intercept?"do":"do not",
             cfg->intercept_mode==INTERCEPT_ONLY?"INTERCEPT_ONLY":"INTERCEPT_EXCEPT",
             cfg->intercept_pattern);
#endif

        /* set up ssl layer */
        if (req->is_ssl) {

                if (use_proxy) {
                        char *connect_buf = NULL;

                        /* 0. set up proxy->proxy ssl session (i.e. forward CONNECT request) */
                        retcode = proxenet_write(*client_sock, req->data, req->size);
                        if (retcode < 0) {
                                xlog(LOG_ERROR, "%s failed to CONNECT to proxy\n", PROGNAME);
                                return -1;
                        }

                        /* read response */
                        retcode = proxenet_read_all(*client_sock, &connect_buf, NULL);
                        if (retcode < 0) {
                                xlog(LOG_ERROR, "%s Failed to read from proxy\n", PROGNAME);
                                return -1;
                        }

                        /* expect HTTP 200 */
                        if (   (strncmp(connect_buf, "HTTP/1.0 200", 12) != 0)
                               && (strncmp(connect_buf, "HTTP/1.1 200", 12) != 0)) {
                                xlog(LOG_ERROR, "%s->proxy: bad HTTP version\n", PROGNAME);
                                if (cfg->verbose)
                                        xlog(LOG_ERROR, "Received %s\n", connect_buf);

                                return -1;
                        }
#ifdef DEBUG
                        xlog(LOG_DEBUG, "HTTP Connect Ok with '%s:%s', cli_sock=%d\n", host, port, *client_sock);
#endif
                }

                if (req->do_intercept){

                        /* 1. set up proxy->server ssl session with hostname */
                        if(proxenet_ssl_init_client_context(&(ssl_ctx->client), http_infos->hostname) < 0) {
                                return -1;
                        }

                        proxenet_ssl_wrap_socket(&(ssl_ctx->client.context), client_sock);
                        if (proxenet_ssl_handshake(&(ssl_ctx->client.context)) < 0) {
                                xlog(LOG_ERROR, "%s->'%s:%d': handshake failed\n",
                                     PROGNAME, http_infos->hostname, http_infos->port);
                                return -1;
                        }

#ifdef DEBUG
                        xlog(LOG_DEBUG, "SSL handshake with %s done, cli_sock=%d\n",
                             use_proxy?"proxy":"server", *client_sock);
#endif
                }

                if (proxenet_write(*server_sock, "HTTP/1.0 200 Connection established\r\n\r\n", 39) < 0){
                        return -1;
                }

                if (req->do_intercept) {

                        /* 2. set up proxy->browser ssl session with hostname */
                        if(proxenet_ssl_init_server_context(&(ssl_ctx->server), http_infos->hostname) < 0) {
                                return -1;
                        }

                        proxenet_ssl_wrap_socket(&(ssl_ctx->server.context), server_sock);
                        if (proxenet_ssl_handshake(&(ssl_ctx->server.context)) < 0) {
                                xlog(LOG_ERROR, "handshake %s->client failed\n", PROGNAME);
                                return -1;
                        }

#ifdef DEBUG
                        xlog(LOG_DEBUG, "SSL handshake with client done, srv_sock=%d\n", *server_sock);
#endif
                }
        }


	return retcode;
}
