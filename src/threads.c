#include "internal.h"

#include <time.h>

/* Tells the core we are spinning, so a hyperthread sibling doing real work runs at full speed. */
#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
#include <immintrin.h>
#define SPIN_PAUSE() _mm_pause()
#elif (defined(__aarch64__) || defined(__arm__)) && defined(__GNUC__)
#define SPIN_PAUSE() __asm__ __volatile__("yield")
#else
#define SPIN_PAUSE() ((void)0)
#endif

int sl__chunks(int count) { return (count + SL_CHUNK - 1) / SL_CHUNK; }

#ifdef SLIME_PROFILE
double sl__prof[P_COUNT];
long sl__dispatches;
double sl__now(void) { struct timespec t; timespec_get(&t, TIME_UTC); return (double)t.tv_sec + (double)t.tv_nsec * 1e-9; }
#endif

typedef struct {
    sl_world *w;
    sl_range_fn fn;
    void *ctx;
    int count, size;
} range_job;

/* Chunks may run on worker threads or a game's own job threads, whose float mode slime sets for the run. */
static void run_range(int first, int last, void *ctx) {
    range_job *j = ctx;
    sl_fpmode fpmode = sl_fp_enter();
    for (int c = first; c < last; c++) {
        int begin = c * j->size, end = begin + j->size;
        j->fn(j->w, begin, end < j->count ? end : j->count, c, j->ctx);
    }
    sl_fp_leave(fpmode);
}

/* A job lives in one 64-bit word: generation, chunk count and next chunk to hand out. A worker claims a
   chunk with one compare and swap, so it can never take a chunk of a job that has already moved on. */
#define JOB_BITS 22
#define JOB_MASK ((1ull << JOB_BITS) - 1)
#define JOB_PACK(gen, count) (((unsigned long long)(gen) << (2 * JOB_BITS)) | ((unsigned long long)(count) << JOB_BITS))
#define JOB_COUNT(v) (((v) >> JOB_BITS) & JOB_MASK)
#define JOB_NEXT(v) ((v) & JOB_MASK)

void sl__parallel_sized(sl_world *w, int count, int size, sl_range_fn fn, void *ctx) {
    int chunks = (count + size - 1) / size;
    if (chunks <= 0) return;
    range_job job = {w, fn, ctx, count, size};
    if ((unsigned long long)chunks > JOB_MASK) { run_range(0, chunks, &job); return; }
#ifdef SLIME_PROFILE
    sl__dispatches++;
#endif
    if (chunks == 1 || (!w->pool && !w->tasks.parallel_for)) run_range(0, chunks, &job);
    else if (w->tasks.parallel_for) w->tasks.parallel_for(run_range, chunks, &job, w->tasks.user);
    else sl__pool_run(w->pool, run_range, chunks, &job);
}

void sl__parallel(sl_world *w, int count, sl_range_fn fn, void *ctx) { sl__parallel_sized(w, count, SL_CHUNK, fn, ctx); }

/* Workers spin this many pauses between jobs, which covers the short gaps between passes, then block so a long
   serial phase or the time between steps does not cost a core per worker. */
#define SPIN_LIMIT 4000

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

struct sl_pool {
    int count;
    HANDLE *threads;
    CRITICAL_SECTION lock;
    CONDITION_VARIABLE cv;
    volatile LONG64 work;
    volatile LONG done, quit, sleepers;
    sl_task_fn *volatile task;
    void *volatile ctx;
    unsigned gen;
    int inited;
};

static unsigned long long load_work(sl_pool *p) { return (unsigned long long)InterlockedCompareExchange64(&p->work, 0, 0); }
static int has_work(sl_pool *p) { unsigned long long v = load_work(p); return JOB_NEXT(v) < JOB_COUNT(v); }

static int take_chunks(sl_pool *p) {
    int took = 0;
    for (;;) {
        unsigned long long v = load_work(p);
        if (JOB_NEXT(v) >= JOB_COUNT(v)) return took;
        if ((unsigned long long)InterlockedCompareExchange64(&p->work, (LONG64)(v + 1), (LONG64)v) != v) continue;
        int c = (int)JOB_NEXT(v);
        p->task(c, c + 1, p->ctx);
        InterlockedIncrement(&p->done);
        took = 1;
    }
}

static DWORD WINAPI worker(LPVOID arg) {
    sl_pool *p = arg;
    int spins = 0;
    while (!InterlockedCompareExchange(&p->quit, 0, 0)) {
        if (take_chunks(p)) { spins = 0; continue; }
        if (++spins < SPIN_LIMIT) { SPIN_PAUSE(); continue; }
        /* Counted as a sleeper before the last look for work, so pool_run either sees us or we see its job. */
        InterlockedIncrement(&p->sleepers);
        EnterCriticalSection(&p->lock);
        while (!InterlockedCompareExchange(&p->quit, 0, 0) && !has_work(p)) SleepConditionVariableCS(&p->cv, &p->lock, INFINITE);
        LeaveCriticalSection(&p->lock);
        InterlockedDecrement(&p->sleepers);
        spins = 0;
    }
    return 0;
}

sl_pool *sl__pool_create(sl_world *w, int threads) {
    sl_pool *p = sl__alloc(w, sizeof *p);
    if (!p) return NULL;
    ZeroMemory(p, sizeof *p);
    p->threads = sl__alloc(w, sizeof(HANDLE) * (size_t)threads);
    if (!p->threads) { sl__pool_destroy(w, p); return NULL; }
    InitializeCriticalSection(&p->lock);
    InitializeConditionVariable(&p->cv);
    p->inited = 1;
    for (int i = 0; i < threads; i++) {
        p->threads[p->count] = CreateThread(NULL, 0, worker, p, 0, NULL);
        if (p->threads[p->count]) p->count++;
    }
    return p;
}

void sl__pool_destroy(sl_world *w, sl_pool *p) {
    if (!p) return;
    if (p->inited) {
        EnterCriticalSection(&p->lock);
        InterlockedExchange(&p->quit, 1);
        WakeAllConditionVariable(&p->cv);
        LeaveCriticalSection(&p->lock);
        for (int i = 0; i < p->count; i++) { WaitForSingleObject(p->threads[i], INFINITE); CloseHandle(p->threads[i]); }
        DeleteCriticalSection(&p->lock);
    }
    sl__free(w, p->threads);
    sl__free(w, p);
}

void sl__pool_run(sl_pool *p, sl_task_fn *task, int count, void *ctx) {
    p->task = task;
    p->ctx = ctx;
    InterlockedExchange(&p->done, 0);
    p->gen++;
    InterlockedExchange64(&p->work, (LONG64)JOB_PACK(p->gen, count));
    if (InterlockedCompareExchange(&p->sleepers, 0, 0)) {
        EnterCriticalSection(&p->lock);
        WakeAllConditionVariable(&p->cv);
        LeaveCriticalSection(&p->lock);
    }
    take_chunks(p);
    for (int spins = 0; InterlockedCompareExchange(&p->done, 0, 0) < count;) if (++spins > 2000) { SwitchToThread(); spins = 0; } else SPIN_PAUSE();
}

#else

#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>

struct sl_pool {
    int count;
    pthread_t *threads;
    pthread_mutex_t lock;
    pthread_cond_t cv;
    _Atomic unsigned long long work;
    atomic_int done, quit, sleepers;
    sl_task_fn *_Atomic task;
    void *_Atomic ctx;
    unsigned gen;
    int inited;
};

static int has_work(sl_pool *p) { unsigned long long v = atomic_load(&p->work); return JOB_NEXT(v) < JOB_COUNT(v); }

/* Runs chunks of the current job until none are left; the task is read only after a successful claim,
   when the job cannot have been replaced yet. */
static int take_chunks(sl_pool *p) {
    int took = 0;
    unsigned long long v = atomic_load(&p->work);
    while (JOB_NEXT(v) < JOB_COUNT(v)) {
        if (!atomic_compare_exchange_weak(&p->work, &v, v + 1)) continue;
        int c = (int)JOB_NEXT(v);
        atomic_load(&p->task)(c, c + 1, atomic_load(&p->ctx));
        atomic_fetch_add(&p->done, 1);
        took = 1;
        v = atomic_load(&p->work);
    }
    return took;
}

static void *worker(void *arg) {
    sl_pool *p = arg;
    int spins = 0;
    while (!atomic_load(&p->quit)) {
        if (take_chunks(p)) { spins = 0; continue; }
        if (++spins < SPIN_LIMIT) { SPIN_PAUSE(); continue; }
        /* Counted as a sleeper before the last look for work, so pool_run either sees us or we see its job. */
        atomic_fetch_add(&p->sleepers, 1);
        pthread_mutex_lock(&p->lock);
        while (!atomic_load(&p->quit) && !has_work(p)) pthread_cond_wait(&p->cv, &p->lock);
        pthread_mutex_unlock(&p->lock);
        atomic_fetch_sub(&p->sleepers, 1);
        spins = 0;
    }
    return NULL;
}

sl_pool *sl__pool_create(sl_world *w, int threads) {
    sl_pool *p = sl__alloc(w, sizeof *p);
    if (!p) return NULL;
    p->count = 0;
    p->inited = 0;
    p->gen = 0;
    p->threads = sl__alloc(w, sizeof(pthread_t) * (size_t)threads);
    if (!p->threads) { sl__pool_destroy(w, p); return NULL; }
    pthread_mutex_init(&p->lock, NULL);
    pthread_cond_init(&p->cv, NULL);
    atomic_init(&p->work, 0);
    atomic_init(&p->done, 0);
    atomic_init(&p->quit, 0);
    atomic_init(&p->sleepers, 0);
    atomic_init(&p->task, NULL);
    atomic_init(&p->ctx, NULL);
    p->inited = 1;
    for (int i = 0; i < threads; i++)
        if (pthread_create(&p->threads[p->count], NULL, worker, p) == 0) p->count++;
    return p;
}

void sl__pool_destroy(sl_world *w, sl_pool *p) {
    if (!p) return;
    if (p->inited) {
        pthread_mutex_lock(&p->lock);
        atomic_store(&p->quit, 1);
        pthread_cond_broadcast(&p->cv);
        pthread_mutex_unlock(&p->lock);
        for (int i = 0; i < p->count; i++) pthread_join(p->threads[i], NULL);
        pthread_mutex_destroy(&p->lock);
        pthread_cond_destroy(&p->cv);
    }
    sl__free(w, p->threads);
    sl__free(w, p);
}

/* Workers only bump done after finishing a claimed chunk, so once done reaches count nobody still
   touches this job and the next one can be published. */
void sl__pool_run(sl_pool *p, sl_task_fn *task, int count, void *ctx) {
    atomic_store(&p->task, task);
    atomic_store(&p->ctx, ctx);
    atomic_store(&p->done, 0);
    p->gen++;
    atomic_store(&p->work, JOB_PACK(p->gen, count));
    /* Sleepers re-check for work under the lock, so a broadcast under it cannot be missed. */
    if (atomic_load(&p->sleepers)) {
        pthread_mutex_lock(&p->lock);
        pthread_cond_broadcast(&p->cv);
        pthread_mutex_unlock(&p->lock);
    }
    take_chunks(p);
    for (int spins = 0; atomic_load(&p->done) < count;) if (++spins > 2000) { sched_yield(); spins = 0; } else SPIN_PAUSE();
}

#endif
