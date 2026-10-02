#include "internal.h"

int sl__chunks(int count) { return (count + SL_CHUNK - 1) / SL_CHUNK; }

typedef struct {
    sl_world *w;
    sl_range_fn fn;
    void *ctx;
    int count;
} range_job;

static void run_range(int first, int last, void *ctx) {
    range_job *j = ctx;
    for (int c = first; c < last; c++) {
        int begin = c * SL_CHUNK, end = begin + SL_CHUNK;
        j->fn(j->w, begin, end < j->count ? end : j->count, c, j->ctx);
    }
}

void sl__parallel(sl_world *w, int count, sl_range_fn fn, void *ctx) {
    int chunks = sl__chunks(count);
    if (chunks <= 0) return;
    range_job job = {w, fn, ctx, count};
    if (chunks == 1 || (!w->pool && !w->tasks.parallel_for)) run_range(0, chunks, &job);
    else if (w->tasks.parallel_for) w->tasks.parallel_for(run_range, chunks, &job, w->tasks.user);
    else pool_run(w->pool, run_range, chunks, &job);
}

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef struct { struct sl_pool *pool; int index; } worker_arg;

struct sl_pool {
    int count;
    HANDLE *threads;
    CRITICAL_SECTION lock;
    CONDITION_VARIABLE cv;
    volatile LONG gen, next, done, awake, quit;
    volatile LONG *ack;   /* last job each worker finished looking at */
    sl_task_fn *task;
    void *ctx;
    volatile LONG chunks;
    worker_arg *args;
    int inited;
};

static void take_chunks(sl_pool *p) {
    for (;;) {
        LONG c = InterlockedIncrement(&p->next) - 1;
        if (c >= p->chunks) return;
        p->task((int)c, (int)c + 1, p->ctx);
        InterlockedIncrement(&p->done);
    }
}

static DWORD WINAPI worker(LPVOID arg) {
    sl_pool *p = ((worker_arg *)arg)->pool;
    int me = ((worker_arg *)arg)->index;
    LONG seen = 0;
    for (;;) {
        EnterCriticalSection(&p->lock);
        while (!p->awake && !p->quit) SleepConditionVariableCS(&p->cv, &p->lock, INFINITE);
        LeaveCriticalSection(&p->lock);
        if (p->quit) return 0;
        int spins = 0;
        while (p->awake) {
            LONG g = InterlockedCompareExchange(&p->gen, 0, 0);
            if (g != seen) { seen = g; take_chunks(p); InterlockedExchange(&p->ack[me], g); spins = 0; }
            else if (++spins < 2000) YieldProcessor();
            else SwitchToThread();
        }
    }
}

sl_pool *pool_create(sl_world *w, int threads) {
    sl_pool *p = sl__alloc(w, sizeof *p);
    if (!p) return NULL;
    ZeroMemory(p, sizeof *p);
    p->threads = sl__alloc(w, sizeof(HANDLE) * (size_t)threads);
    p->ack = sl__alloc(w, sizeof(LONG) * (size_t)threads);
    p->args = sl__alloc(w, sizeof(worker_arg) * (size_t)threads);
    if (!p->threads || !p->ack || !p->args) { pool_destroy(w, p); return NULL; }
    InitializeCriticalSection(&p->lock);
    InitializeConditionVariable(&p->cv);
    p->inited = 1;
    for (int i = 0; i < threads; i++) {
        p->ack[p->count] = 0;
        p->args[p->count] = (worker_arg){p, p->count};
        p->threads[p->count] = CreateThread(NULL, 0, worker, &p->args[p->count], 0, NULL);
        if (p->threads[p->count]) p->count++;
    }
    return p;
}

void pool_destroy(sl_world *w, sl_pool *p) {
    if (!p) return;
    if (p->inited) {
        EnterCriticalSection(&p->lock);
        p->quit = 1;
        p->awake = 0;
        WakeAllConditionVariable(&p->cv);
        LeaveCriticalSection(&p->lock);
        for (int i = 0; i < p->count; i++) { WaitForSingleObject(p->threads[i], INFINITE); CloseHandle(p->threads[i]); }
        DeleteCriticalSection(&p->lock);
    }
    sl__free(w, p->threads);
    sl__free(w, (void *)p->ack);
    sl__free(w, p->args);
    sl__free(w, p);
}

void pool_wake(sl_pool *p) {
    EnterCriticalSection(&p->lock);
    p->awake = 1;
    WakeAllConditionVariable(&p->cv);
    LeaveCriticalSection(&p->lock);
}

void pool_sleep(sl_pool *p) { InterlockedExchange(&p->awake, 0); }

void pool_run(sl_pool *p, sl_task_fn *task, int count, void *ctx) {
    p->task = task;
    p->ctx = ctx;
    p->chunks = count;
    InterlockedExchange(&p->done, 0);
    InterlockedExchange(&p->next, 0);
    LONG g = InterlockedIncrement(&p->gen);
    take_chunks(p);
    for (int spins = 0; InterlockedCompareExchange(&p->done, 0, 0) < count;) if (++spins > 2000) { SwitchToThread(); spins = 0; }
    for (int i = 0; i < p->count; i++)
        for (int spins = 0; InterlockedCompareExchange(&p->ack[i], 0, 0) != g;) if (++spins > 2000) { SwitchToThread(); spins = 0; }
}

#else

#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>

typedef struct { struct sl_pool *pool; int index; } worker_arg;

struct sl_pool {
    int count;
    pthread_t *threads;
    pthread_mutex_t lock;
    pthread_cond_t cv;
    atomic_int gen, next, done, awake, quit, chunks;
    atomic_int *ack;   /* last job each worker finished looking at */
    worker_arg *args;
    sl_task_fn *_Atomic task;
    void *_Atomic ctx;
    int inited;
};

static void take_chunks(sl_pool *p) {
    for (;;) {
        int c = atomic_fetch_add(&p->next, 1);
        if (c >= atomic_load(&p->chunks)) return;
        atomic_load(&p->task)(c, c + 1, atomic_load(&p->ctx));
        atomic_fetch_add(&p->done, 1);
    }
}

static void *worker(void *arg) {
    sl_pool *p = ((worker_arg *)arg)->pool;
    int me = ((worker_arg *)arg)->index;
    int seen = 0;
    for (;;) {
        pthread_mutex_lock(&p->lock);
        while (!atomic_load(&p->awake) && !atomic_load(&p->quit)) pthread_cond_wait(&p->cv, &p->lock);
        pthread_mutex_unlock(&p->lock);
        if (atomic_load(&p->quit)) return NULL;
        /* Spin while a step is running: passes are short, so waking from sleep would cost more. */
        int spins = 0;
        while (atomic_load(&p->awake)) {
            int g = atomic_load(&p->gen);
            if (g != seen) { seen = g; take_chunks(p); atomic_store(&p->ack[me], g); spins = 0; }
            else if (++spins > 2000) { sched_yield(); spins = 0; }
        }
    }
}

sl_pool *pool_create(sl_world *w, int threads) {
    sl_pool *p = sl__alloc(w, sizeof *p);
    if (!p) return NULL;
    p->count = 0;
    p->inited = 0;
    p->threads = sl__alloc(w, sizeof(pthread_t) * (size_t)threads);
    p->ack = sl__alloc(w, sizeof(atomic_int) * (size_t)threads);
    p->args = sl__alloc(w, sizeof(worker_arg) * (size_t)threads);
    if (!p->threads || !p->ack || !p->args) { pool_destroy(w, p); return NULL; }
    pthread_mutex_init(&p->lock, NULL);
    pthread_cond_init(&p->cv, NULL);
    atomic_init(&p->gen, 0);
    atomic_init(&p->next, 0);
    atomic_init(&p->done, 0);
    atomic_init(&p->awake, 0);
    atomic_init(&p->quit, 0);
    atomic_init(&p->chunks, 0);
    atomic_init(&p->task, NULL);
    atomic_init(&p->ctx, NULL);
    p->inited = 1;
    for (int i = 0; i < threads; i++) {
        atomic_init(&p->ack[p->count], 0);
        p->args[p->count] = (worker_arg){p, p->count};
        if (pthread_create(&p->threads[p->count], NULL, worker, &p->args[p->count]) == 0) p->count++;
    }
    return p;
}

void pool_destroy(sl_world *w, sl_pool *p) {
    if (!p) return;
    if (p->inited) {
        pthread_mutex_lock(&p->lock);
        atomic_store(&p->quit, 1);
        atomic_store(&p->awake, 0);
        pthread_cond_broadcast(&p->cv);
        pthread_mutex_unlock(&p->lock);
        for (int i = 0; i < p->count; i++) pthread_join(p->threads[i], NULL);
        pthread_mutex_destroy(&p->lock);
        pthread_cond_destroy(&p->cv);
    }
    sl__free(w, p->threads);
    sl__free(w, p->ack);
    sl__free(w, p->args);
    sl__free(w, p);
}

void pool_wake(sl_pool *p) {
    pthread_mutex_lock(&p->lock);
    atomic_store(&p->awake, 1);
    pthread_cond_broadcast(&p->cv);
    pthread_mutex_unlock(&p->lock);
}

void pool_sleep(sl_pool *p) { atomic_store(&p->awake, 0); }

void pool_run(sl_pool *p, sl_task_fn *task, int count, void *ctx) {
    atomic_store(&p->task, task);
    atomic_store(&p->ctx, ctx);
    atomic_store(&p->chunks, count);
    atomic_store(&p->done, 0);
    atomic_store(&p->next, 0);
    int g = atomic_fetch_add(&p->gen, 1) + 1;
    take_chunks(p);
    for (int spins = 0; atomic_load(&p->done) < count;) if (++spins > 2000) { sched_yield(); spins = 0; }
    /* No worker may still be inside take_chunks when the next job resets the counters. */
    for (int i = 0; i < p->count; i++)
        for (int spins = 0; atomic_load(&p->ack[i]) != g;) if (++spins > 2000) { sched_yield(); spins = 0; }
}

#endif
