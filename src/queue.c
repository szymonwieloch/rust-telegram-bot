/**
 * queue — Thread-safe FIFO queue for Telegram response messages.
 *
 * Used by the responder subsystem to decouple weather-fetch completion
 * (on arbitrary Tokio worker threads) from Telegram message delivery
 * (on a dedicated sender thread).
 */

#include <stdlib.h>
#include <string.h>

#include "queue.h"

/* ── Queue operations ─────────────────────────────────────────────────────── */

void queue_init(response_queue_t *q, apr_pool_t *pool) {
    q->head = NULL;
    q->tail = NULL;
    q->shutdown = 0;
    apr_thread_mutex_create(&q->lock, APR_THREAD_MUTEX_DEFAULT, pool);
    apr_thread_cond_create(&q->cond, pool);
}

void queue_destroy(response_queue_t *q) {
    while (q->head) {
        response_node_t *n = q->head;
        q->head = n->next;
        free(n->text);
        free(n);
    }
    apr_thread_mutex_destroy(q->lock);
    apr_thread_cond_destroy(q->cond);
}

void queue_push(response_queue_t *q, long long chat_id,
                const char *text) {
    response_node_t *n = malloc(sizeof(*n));
    n->chat_id = chat_id;
    n->text    = strdup(text);
    n->next    = NULL;

    apr_thread_mutex_lock(q->lock);
    if (q->tail) {
        q->tail->next = n;
        q->tail = n;
    } else {
        q->head = q->tail = n;
    }
    apr_thread_cond_signal(q->cond);
    apr_thread_mutex_unlock(q->lock);
}

int queue_pop(response_queue_t *q, response_node_t *out) {
    apr_thread_mutex_lock(q->lock);
    while (!q->head && !q->shutdown) {
        apr_thread_cond_wait(q->cond, q->lock);
    }
    if (q->shutdown && !q->head) {
        apr_thread_mutex_unlock(q->lock);
        return -1;
    }
    response_node_t *n = q->head;
    q->head = n->next;
    if (!q->head) q->tail = NULL;
    apr_thread_mutex_unlock(q->lock);

    out->chat_id = n->chat_id;
    out->text    = n->text;
    free(n);
    return 0;
}

void queue_signal_shutdown(response_queue_t *q) {
    apr_thread_mutex_lock(q->lock);
    q->shutdown = 1;
    apr_thread_cond_signal(q->cond);
    apr_thread_mutex_unlock(q->lock);
}
