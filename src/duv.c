#import "uv.h"
#import <stdlib.h>
#import <assert.h>

typedef void * duv_ptr;

typedef void (*duv_read_cb)(uv_stream_t* stream, void * context, ssize_t nread, char * buff_data, size_t buff_len);

typedef void (*duv__handle_close_cb)(uv_handle_t * handle, void * context);
typedef void (*duv__check_cb)(uv_check_t* handle, void * context, int status);

//
// Handle data pointer wrapper
//
typedef struct {
    void * data;
    duv_read_cb read_cb;
    void * read_context;
    void * close_context;
    duv__handle_close_cb close_cb;
    void * check_context;
    duv__check_cb check_cb;
} duv_handle_context;


duv_handle_context * duv_ensure_handle_context(uv_handle_t * handle) {
    if(handle->data == NULL) {
        duv_handle_context * ctx = calloc(1, sizeof(duv_handle_context));
        handle->data = ctx;
    }
  return handle->data;
}

void duv__clean_handle_context(uv_handle_t * handle) {
    if(handle->data) {
        free(handle->data);
        handle->data = NULL;
    }
}

UV_EXTERN void duv_set_handle_data(uv_handle_t* handle, void* data) {
  duv_ensure_handle_context(handle)->data = data;
}

UV_EXTERN void* duv_get_handle_data(uv_handle_t* handle) {
  return duv_ensure_handle_context(handle)->data;
}

UV_EXTERN int duv_tcp_bind4(uv_tcp_t* handle, char *ipv4, int port) {
  struct sockaddr_in addr = uv_ip4_addr(ipv4, port);
  return uv_tcp_bind(handle, addr);
}

typedef void (*duv__write_cb)(uv_stream_t* connection, void * context, int status);

typedef struct  {
  uv_write_t req;
  void * context;
  uv_buf_t * bufs;
  duv__write_cb cb;
  uv_stream_t * connection;
} duv_write_request;



void _duv__write_callback(uv_write_t* req, int status) {
 duv_write_request * request = (duv_write_request*)req;
 request->cb(request->connection, request->context, status);
 free(request->bufs); 
 free(request);
}

UV_EXTERN int duv__write(uv_stream_t* handle, void * context, char * data, int data_len,  duv__write_cb cb) {
  uv_buf_t * bufs = malloc(sizeof(uv_buf_t));
  *bufs = uv_buf_init(data, data_len);

  duv_write_request * req = calloc(1, sizeof(duv_write_request));
  req->connection = handle;
  req->context = context;
  req->bufs = bufs;
  req->cb = cb;
  return uv_write((uv_write_t*)req, handle, req->bufs, 1, _duv__write_callback);
}

uv_buf_t duv__alloc_cb(uv_handle_t* handle, size_t suggested_size) {
  return uv_buf_init(malloc(suggested_size), suggested_size);
}

void duv__read_cb_bridge(uv_stream_t* stream, ssize_t nread, uv_buf_t buf) {
  duv_handle_context * handle_context = duv_ensure_handle_context((uv_handle_t*)stream);
  handle_context->read_cb(stream, handle_context->read_context, nread, buf.base, buf.len);
  free(buf.base);
}

UV_EXTERN int duv__read_start(uv_stream_t * stream, void * context, duv_read_cb read_cb) {
  duv_handle_context * handle_context = duv_ensure_handle_context((uv_handle_t*)stream);
  handle_context->read_cb = read_cb;
  handle_context->read_context = context;
  return uv_read_start(stream, &duv__alloc_cb, &duv__read_cb_bridge);
}

UV_EXTERN int duv__read_stop(uv_stream_t* stream, void ** readContext) {
  duv_handle_context * handle_context = duv_ensure_handle_context((uv_handle_t*)stream);
  *readContext = handle_context->read_context;
  handle_context->read_context = NULL;
  handle_context->read_cb = NULL;
  return uv_read_stop(stream);
}


void duv__handle_close_async_cb_bridge(uv_handle_t * handle);

void duv__handle_close_cb_bridge(uv_handle_t * handle) {
  duv_handle_context * handle_context = duv_ensure_handle_context(handle);
  handle_context->close_cb(handle, handle_context->close_context);
  duv__handle_close_async_cb_bridge(handle);
}

UV_EXTERN void duv__handle_close(uv_handle_t * handle, void * context, duv__handle_close_cb close_cb) {
  duv_handle_context * handle_context = duv_ensure_handle_context(handle);
  handle_context->close_cb = close_cb;
  handle_context->close_context = context;
  uv_close(handle, &duv__handle_close_cb_bridge);
}

void duv__handle_close_async_cb_bridge(uv_handle_t * handle) {
  duv__clean_handle_context(handle);
  free(handle);
}

UV_EXTERN void duv__handle_close_async(uv_handle_t * handle) {
  uv_close(handle, &duv__handle_close_async_cb_bridge);
}

UV_EXTERN void* duv__handle_alloc(uv_handle_type type) {
    return calloc(1, uv_handle_size(type));
}


void duv__check_bridge_cb(uv_check_t * handle, int status) {
    duv_handle_context * handle_context = duv_ensure_handle_context((uv_handle_t*)handle);
    handle_context->check_cb(handle, handle_context->check_context, status);
}

UV_EXTERN int duv__check_start(uv_check_t* handle, void * context, duv__check_cb cb) {
    duv_handle_context * handle_context = duv_ensure_handle_context((uv_handle_t*)handle);
    handle_context->check_context = context;
    handle_context->check_cb = cb;
    return uv_check_start(handle, &duv__check_bridge_cb);
}

UV_EXTERN void* duv__check_get_context(uv_check_t* handle) {
    duv_handle_context * handle_context = duv_ensure_handle_context((uv_handle_t*)handle);
    return handle_context->check_context;
}

UV_EXTERN int duv__check_stop(uv_check_t* check) {
    return uv_check_stop(check);
}

typedef void (*duv_connect_callback)(uv_tcp_t* handle, void* context, int status);

typedef struct {
    uv_connect_t req;
    void * context;
    duv_connect_callback callback;
} duv_connect_t;


void duv__connect_cb_bridge(uv_connect_t* plain_req, int status) {
    duv_connect_t * req = (duv_connect_t*)plain_req;
    req->callback((uv_tcp_t*)req->req.handle, req->context, status);
    free(req);
}

UV_EXTERN int duv__tcp_connect4(uv_tcp_t* handle, void * context, char * ipv4, int port, duv_connect_callback cb) {
  struct sockaddr_in addr = uv_ip4_addr(ipv4, port);
  duv_connect_t * req = (duv_connect_t*)calloc(1, sizeof(duv_connect_t));
  req->context = context;
  req->callback = cb;
  return uv_tcp_connect((uv_connect_t*)req, handle, addr, &duv__connect_cb_bridge);
}

typedef void (*duv__uv_getaddrinfo_callback)(void * context, int status, struct addrinfo* res);

typedef struct {
    uv_getaddrinfo_t req;
    void * context;
    duv__uv_getaddrinfo_callback callback;
} duv_getaddrinfo_t;

void duv__uv_getaddrinfo_bridge(uv_getaddrinfo_t* r, int status, struct addrinfo* res) {
    duv_getaddrinfo_t* req = (duv_getaddrinfo_t*)r;
    req->callback(req->context, status, res);
    if(res) {
        uv_freeaddrinfo(res);
    }
    free(req);
}

UV_EXTERN int duv__uv_getaddrinfo(uv_loop_t * loop, void* context, const char * node, const char * service, duv__uv_getaddrinfo_callback cb) {
   duv_getaddrinfo_t * req = (duv_getaddrinfo_t*)calloc(1, sizeof(duv_getaddrinfo_t));
   req->context = context;
   req->callback = cb;
   return uv_getaddrinfo(loop, (uv_getaddrinfo_t*)req, &duv__uv_getaddrinfo_bridge, node, service, NULL);
}

UV_EXTERN int duv__getaddr_ip(struct addrinfo* addr, char * ip, size_t ip_len) {
    if(addr->ai_family == AF_INET) {
        struct sockaddr_in* ipv = (struct sockaddr_in*)addr->ai_addr;
        return uv_ip4_name(ipv, ip, ip_len);
    }
    else if(addr->ai_family == AF_INET6) {
        struct sockaddr_in6* ipv = (struct sockaddr_in6*)addr->ai_addr;
        return uv_ip6_name(ipv, ip, ip_len);
    } else {
        assert(0 && "Unknown address type");
    }
}

typedef void (*duv__fs_open_callback)(void * context, int status, void * fd);

typedef struct {
    uv_fs_t req;
    void * context;
    duv__fs_open_callback callback;
} duv__fs_req_t;

void duv__fs_open_bridge(uv_fs_t* uv_req) {
    duv__fs_req_t * req = (duv__fs_req_t*)uv_req;
    ssize_t status = req->req.result;
    void * fd = NULL;
    if(status > 0) {
        fd = (void*)status;
    }
    req->callback(req->context, status, fd);
    free(req);
}

UV_EXTERN int duv__fs_open(uv_loop_t* loop, void * context,  const char* path,
            int flags, int mode, duv__fs_open_callback cb) {
   duv__fs_req_t * req = calloc(1, sizeof(duv__fs_req_t));
   req->context = context;
   req->callback = cb;
   return uv_fs_open(loop, &req->req, path, flags, mode, &duv__fs_open_bridge);
}

