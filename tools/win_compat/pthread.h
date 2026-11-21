// The preview is single-threaded: the store's headers only need the types of
// the members their classes declare.
#pragma once
typedef struct { void* handle; } pthread_mutex_t;
typedef struct { void* handle; } pthread_cond_t;
typedef void* pthread_t;
#define PTHREAD_MUTEX_INITIALIZER { 0 }
