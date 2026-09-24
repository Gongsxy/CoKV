/*
 * proto_test_hiredis.c -- protocol test cases for kvstore driven by hiredis
 *
 *   Coverage: special chars / binary safety / pipeline batch
 *
 * Dependency: sudo apt install libhiredis-dev
 * Build:      gcc -O2 -Wall -Wextra -o proto_test_hiredis proto_test_hiredis.c -lhiredis
 * Run:        ./proto_test_hiredis [ip] [port] [batch]     default 127.0.0.1 2000 1000
 *
 * Exit code: 0 = all passed, otherwise = number of failures
 *
 * Notes:
 *   - All test cases run on a single connection / single server session;
 *     do not restart the server in the middle.
 *   - Server must have show_response = 1.
 *   - KVS_TEST_MAX_VALUE must match KVS_MAX_VALUE in server.h.
 *   - hiredis %b is (pointer, length) and binary safe; read replies via
 *     reply->str + reply->len, not strlen.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <hiredis/hiredis.h>

#ifndef KVS_TEST_MAX_VALUE
#define KVS_TEST_MAX_VALUE  (64 * 1024)
#endif
#define KVS_TEST_OVER_VALUE (KVS_TEST_MAX_VALUE * 2)
#define BATCH_DEFAULT       1000
#define KLIT(s)             (sizeof(s) - 1)

static int g_pass = 0;
static int g_fail = 0;
static redisContext *ctx = NULL;

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static void ok(const char *name, int cond, const char *detail)
{
    if (cond) { printf("  [PASS] %-30s %s\n", name, detail ? detail : ""); g_pass++; }
    else      { printf("  [FAIL] %-30s %s\n", name, detail ? detail : ""); g_fail++; }
}

static unsigned char *pattern_buf(size_t n)
{
    unsigned char *p = malloc(n ? n : 1);
    for (size_t i = 0; i < n; i++)
        p[i] = (unsigned char)(i * 131 + 7);       /* covers 0x00..0xff */
    return p;
}

static void make_key(char *dst, size_t cap, int i)
{
    snprintf(dst, cap, "batch:%d", i);
}

/* ------------------------------------------------------------- cases */

static void case_ping(void)
{
    redisReply *r = redisCommand(ctx, "PING");
    if (!r) { ok("PING connectivity", 0, ctx->errstr); return; }
    ok("PING connectivity",
       r->type == REDIS_REPLY_STATUS && r->len == 4 && memcmp(r->str, "PONG", 4) == 0,
       "+PONG");
    freeReplyObject(r);
}

/* SET then GET, byte-by-byte comparison (%b is binary safe) */
static void case_setget(const char *name, const char *key, size_t klen,
                        const char *val, size_t vlen)
{
    redisReply *r = redisCommand(ctx, "SET %b %b", key, klen, val, vlen);
    if (!r) { ok(name, 0, ctx->errstr); return; }
    if (r->type == REDIS_REPLY_ERROR) {
        char d[128];
        snprintf(d, sizeof d, "SET rejected: %s", r->str ? r->str : "");
        ok(name, 0, d);
        freeReplyObject(r);
        return;
    }
    freeReplyObject(r);

    r = redisCommand(ctx, "GET %b", key, klen);
    if (!r) { ok(name, 0, ctx->errstr); return; }
    if (r->type != REDIS_REPLY_STRING) {
        ok(name, 0, "not bulk (key lost?)");
        freeReplyObject(r);
        return;
    }
    int same = ((size_t)r->len == vlen) && (vlen == 0 || memcmp(r->str, val, vlen) == 0);
    char d[128];
    snprintf(d, sizeof d, "got %zu bytes / expect %zu bytes%s",
             (size_t)r->len, vlen, same ? " , byte-exact" : " , mismatch");
    ok(name, same, d);
    freeReplyObject(r);
}

static void case_missing(void)
{
    redisReply *r = redisCommand(ctx, "GET %b", "no_such_key_at_all", KLIT("no_such_key_at_all"));
    if (!r) { ok("missing key -> nil", 0, ctx->errstr); return; }
    ok("missing key -> nil", r->type == REDIS_REPLY_NIL, "$-1");
    freeReplyObject(r);
}

static void case_oversize(size_t n)
{
    unsigned char *v = pattern_buf(n);
    double t0 = now_ms();

    redisReply *r = redisCommand(ctx, "SET %b %b", "oversize", KLIT("oversize"), v, n);
    double dt = now_ms() - t0;
    free(v);

    if (!r) { ok("oversize no hang/crash", 0, ctx->errstr); return; }

    char d[160];
    if (r->type == REDIS_REPLY_ERROR)
        snprintf(d, sizeof d, "%.0f ms , explicitly rejected: %s", dt, r->str ? r->str : "");
    else
        snprintf(d, sizeof d, "%.0f ms , server limit larger, accepted %zu bytes", dt, n);
    ok("oversize no hang/crash", 1, d);
    freeReplyObject(r);
}

/* append N SETs, then drain replies -- hiredis pipeline */
static void case_batch_set(int batch)
{
    char key[64];
    double t0 = now_ms();

    for (int i = 0; i < batch; i++) {
        make_key(key, sizeof key, i);
        const char *argv[3] = { "SET", key, "v" };
        size_t      alen[3] = { 3, strlen(key), 1 };
        if (redisAppendCommandArgv(ctx, 3, argv, alen) != REDIS_OK)
            { ok("Batch SET (pipeline)", 0, ctx->errstr); return; }
    }

    int good = 0, bad = 0;
    for (int i = 0; i < batch; i++) {
        redisReply *r = NULL;
        if (redisGetReply(ctx, (void **)&r) != REDIS_OK) { bad += batch - i; break; }
        if (r->type == REDIS_REPLY_STATUS && r->len == 2 && memcmp(r->str, "OK", 2) == 0) good++;
        else bad++;
        freeReplyObject(r);
    }
    double dt = now_ms() - t0;

    char d[192];
    snprintf(d, sizeof d, "pipeline %d , got %d +OK , bad %d , %.1f ms",
             batch, good, bad, dt);
    ok("Batch SET (pipeline)", bad == 0 && good == batch, d);
}

/* append N GETs, then verify each reply value */
static void case_batch_get(int batch)
{
    char key[64];

    for (int i = 0; i < batch; i++) {
        make_key(key, sizeof key, i);
        const char *argv[2] = { "GET", key };
        size_t      alen[2] = { 3, strlen(key) };
        if (redisAppendCommandArgv(ctx, 2, argv, alen) != REDIS_OK)
            { ok("Batch GET (pipeline)", 0, ctx->errstr); return; }
    }

    int good = 0, bad = 0;
    for (int i = 0; i < batch; i++) {
        redisReply *r = NULL;
        if (redisGetReply(ctx, (void **)&r) != REDIS_OK) { bad += batch - i; break; }
        if (r->type == REDIS_REPLY_STRING && r->len == 1 && r->str[0] == 'v') good++;
        else bad++;
        freeReplyObject(r);
    }

    char d[192];
    snprintf(d, sizeof d, "pipeline %d , correct %d , bad %d", batch, good, bad);
    ok("Batch GET (pipeline)", bad == 0 && good == batch, d);
}

static void case_batch_qps(int batch, int rounds)
{
    char key[64];
    long total = (long)batch * rounds;

    double t0 = now_ms();
    for (int r = 0; r < rounds; r++) {
        for (int i = 0; i < batch; i++) {
            snprintf(key, sizeof key, "qps:%d:%d", r, i);
            const char *argv[3] = { "SET", key, "v" };
            size_t      alen[3] = { 3, strlen(key), 1 };
            if (redisAppendCommandArgv(ctx, 3, argv, alen) != REDIS_OK) break;
        }
        for (int i = 0; i < batch; i++) {
            redisReply *r = NULL;
            if (redisGetReply(ctx, (void **)&r) != REDIS_OK) break;
            freeReplyObject(r);
        }
    }
    double dt = now_ms() - t0;
    if (dt <= 0) dt = 0.001;

    char d[192];
    snprintf(d, sizeof d, "batch=%d rounds=%d total=%ld , %.1f ms , %.0f ops/s",
             batch, rounds, total, dt, total / (dt / 1000.0));
    ok("Batch throughput (QPS)", 1, d);
}

/* ------------------------------------------------------------------ main */

int main(int argc, char **argv)
{
    const char *ip    = (argc > 1) ? argv[1] : "127.0.0.1";
    int         port  = (argc > 2) ? atoi(argv[2]) : 2000;
    int         batch = (argc > 3) ? atoi(argv[3]) : BATCH_DEFAULT;
    if (batch <= 0) batch = BATCH_DEFAULT;

    printf("=== kvstore hiredis client test %s:%d (batch=%d, max value=%d) ===\n",
           ip, port, batch, KVS_TEST_MAX_VALUE);

    struct timeval tv = { 5, 0 };
    ctx = redisConnectWithTimeout(ip, port, tv);
    if (!ctx || ctx->err) {
        fprintf(stderr, "connect %s:%d failed: %s\n", ip, port, ctx ? ctx->errstr : "alloc fail");
        return 2;
    }
#if defined(HIREDIS_MAJOR)
    printf("hiredis %d.%d.%d\n", HIREDIS_MAJOR, HIREDIS_MINOR, HIREDIS_PATCH);
#else
    printf("hiredis (version macro unavailable)\n");
#endif

    printf("\n-- Special chars & binary safety --\n");
    case_ping();
    case_setget("Basic SET/GET",             "hello", KLIT("hello"), "world", KLIT("world"));
    case_setget("CRLF in key/value",         "a\r\nb", KLIT("a\r\nb"), "x\r\ny", KLIT("x\r\ny"));
    case_setget("Space/quote/RESP metachar", "k 1", KLIT("k 1"),
                "\"d\" $5 *3 -ERR :1 +OK", KLIT("\"d\" $5 *3 -ERR :1 +OK"));
    case_setget("printf format specifiers",  "fmt", KLIT("fmt"), "%s%n%d%%", KLIT("%s%n%d%%"));
    case_setget("UTF-8 + emoji",             "\xe9\x94\xae", KLIT("\xe9\x94\xae"),
                "\xe5\x80\xbc\xf0\x9f\x98\x80", KLIT("\xe5\x80\xbc\xf0\x9f\x98\x80"));
    case_setget("Empty value ($0)",          "empty", KLIT("empty"), "", 0);
    case_setget("Value with \\0",            "nul", KLIT("nul"), "a\0b\0c", KLIT("a\0b\0c"));

    {
        size_t big = KVS_TEST_MAX_VALUE;
        unsigned char *v = pattern_buf(big);
        char name[64];
        snprintf(name, sizeof name, "%zuKB random binary", big / 1024);
        case_setget(name, "big", 3, (char *)v, big);
        free(v);
    }

    {
        char k1k[1025];
        memset(k1k, 'k', 1024); k1k[1024] = '\0';
        case_setget("1KB long key", k1k, 1024, "v", 1);
    }

    case_missing();
    case_oversize(KVS_TEST_OVER_VALUE);

    printf("\n-- Batch (pipeline) --\n");
    case_batch_set(batch);
    case_batch_get(batch);
    case_batch_qps(batch, 20);

    printf("\n=== Result: PASS %d / FAIL %d ===\n", g_pass, g_fail);

    redisFree(ctx);
    return g_fail;
}