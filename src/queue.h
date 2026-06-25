#ifndef QUEUE_H
#define QUEUE_H

#include <apr_pools.h>
#include <apr_thread_mutex.h>
#include <apr_thread_cond.h>

/**
 * A single node in the response queue.
 *
 * Owns a heap-allocated `text` string (strdup'd on push, freed on pop).
 */
typedef struct response_node {
    long long               chat_id;
    char                   *text;
    struct response_node   *next;
} response_node_t;

/**
 * Thread-safe FIFO queue for pending Telegram responses.
 *
 * Producers call queue_push() from any thread.
 * A single consumer calls queue_pop() (blocks until data or shutdown).
 */
typedef struct {
    response_node_t      *head;       /* pop from head */
    response_node_t      *tail;       /* push to tail  */
    apr_thread_mutex_t   *lock;
    apr_thread_cond_t    *cond;
    int                   shutdown;   /* set to 1 to wake the consumer */
} response_queue_t;

/**
 * Initialise the queue and its synchronisation primitives.
 *
 * @param q     Uninitialised queue struct.
 * @param pool  APR pool that owns the mutex and condition variable.
 */
void queue_init(response_queue_t *q, apr_pool_t *pool);

/**
 * Destroy the queue, freeing all remaining nodes and their text.
 *
 * @param q  Queue previously initialised with queue_init().
 */
void queue_destroy(response_queue_t *q);

/**
 * Push a new message onto the tail of the queue (thread-safe).
 *
 * `text` is copied with strdup(); the caller retains ownership of the
 * original.
 *
 * @param q        Target queue.
 * @param chat_id  Telegram chat ID.
 * @param text     Null-terminated response text (copied internally).
 */
void queue_push(response_queue_t *q, long long chat_id, const char *text);

/**
 * Pop the head of the queue, blocking until data is available or the
 * queue is shut down.
 *
 * On success the returned node's `text` is heap-allocated and must be
 * freed by the caller.  The node struct itself is stack-allocated by
 * the caller.
 *
 * @param q    Queue to pop from.
 * @param out  Pointer to caller-allocated node that receives the result.
 * @return     0 on success, -1 if the queue was shut down and is empty.
 */
int queue_pop(response_queue_t *q, response_node_t *out);

/**
 * Signal the queue to shut down.
 *
 * After this call queue_pop() will drain any remaining items and then
 * return -1.  queue_push() still succeeds (items can be drained).
 *
 * @param q  Queue to signal.
 */
void queue_signal_shutdown(response_queue_t *q);

#endif /* QUEUE_H */
