#include <switch.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <memory.h>

static const uint64_t sleepNsec = 1000000000; // 1 sec
static const int numThreads = 4;
static Mutex consoleMtx = 0;

void test_printf(const char *str, ...) {
    mutexLock(&consoleMtx);

    va_list args;
    va_start(args, str);
    vprintf(str, args);
    va_end(args);

    consoleUpdate(NULL);
    mutexUnlock(&consoleMtx);
}

struct ThreadArgs {
    int num;
    Semaphore *sem;
    bool wait;
};

void threadFuncLoop(void *v) {
    struct ThreadArgs *args = v;

    if (args->wait) {
        // Test pause/resume during lock
        semaphoreWait(args->sem);
    } else {
        bool result = semaphoreTryWait(args->sem);
        while (!result) {
            struct timespec time;
            clock_gettime(CLOCK_MONOTONIC, &time);
            test_printf("Thread %d @ %lu\n", args->num, time.tv_sec);
            svcSleepThread(sleepNsec);
            result = semaphoreTryWait(args->sem);
        }
    }

    test_printf("Thread %d exiting\n", args->num);
}

void threadFuncSuspend(void *v) {
    Handle handle = *(Handle *) v;
    test_printf("Suspending main thread...\n");
    svcSetThreadActivity(handle, true);
    svcSleepThread(sleepNsec);
    test_printf("Resuming main thread...\n");
    svcSetThreadActivity(handle, false);
}

#define assertZero(ret) if ((ret) != 0) { \
    result = false; \
    test_printf("%s: Line %d failed with %d\n", __FILE__, __LINE__, (ret)); \
    goto cleanup; \
}

#define assertTrue(ret) if (!(ret)) { \
    result = false; \
    test_printf("%s: Line %d failed with %d\n", __FILE__, __LINE__, (ret)); \
    goto cleanup; \
}

bool threadSuspendTest() {
    bool result = true;

    Result rc;
    Thread threads[numThreads];
    memset(&threads, 0, sizeof(Thread) * numThreads);
    struct ThreadArgs args[numThreads];
    struct timespec start, stop;

    clock_gettime(CLOCK_MONOTONIC, &start);
    test_printf("Started threadSuspendTest @ %lu\n", start.tv_nsec);

    // Test suspending self
    Handle mainThreadHandle = envGetMainThreadHandle();
    assertTrue(mainThreadHandle != 0);
    rc = svcSetThreadActivity(mainThreadHandle, true);
    assertTrue(rc == 0xF401);

    // Test suspending self from other thread
    rc = threadCreate(&threads[0], threadFuncSuspend, &mainThreadHandle, 128 * 1024, 0x3B, -2);
    assertZero(rc);
    rc = threadStart(&threads[0]);
    assertZero(rc);
    rc = threadWaitForExit(&threads[0]);
    assertZero(rc);
    rc = threadClose(&threads[0]);
    assertZero(rc);
    test_printf("Main thread resumed!\n");
    svcSleepThread(sleepNsec);

    // Test suspending multiple threads
    Semaphore threadSem;
    semaphoreInit(&threadSem, 0);
    for (int i = 0; i < numThreads; ++i) {
        args[i] = (struct ThreadArgs) {.num = i + 1, .sem = &threadSem, .wait = i == 3};
    }

    for (int i = 0; i < numThreads; ++i) {
        rc = threadCreate(&threads[i], threadFuncLoop, &args[i], 128 * 1024, 0x3B, -2);
        assertZero(rc);
        rc = threadStart(&threads[i]);
        assertZero(rc);
    }

    svcSleepThread(sleepNsec);
    for (int i = 0; i < numThreads; i += 3) {
        test_printf("Suspending thread %d...\n", i + 1);
        rc = threadPause(&threads[i]);
        assertZero(rc);
    }

    svcSleepThread(sleepNsec * 3);
    for (int i = 0; i < numThreads; i += 3) {
        test_printf("Resuming thread %d...\n", i + 1);
        rc = threadResume(&threads[i]);
        assertZero(rc);
    }

    svcSleepThread(sleepNsec * 3);
    test_printf("Exiting threads...\n");
    for (int i = 0; i < numThreads; ++i) {
        semaphoreSignal(&threadSem);
    }
    for (int i = 0; i < numThreads; ++i) {
        rc = threadWaitForExit(&threads[i]);
        assertZero(rc);
        rc = threadClose(&threads[i]);
        assertZero(rc);
    }

    clock_gettime(CLOCK_MONOTONIC, &stop);
    test_printf("Ended threadSuspendTest @ %lu w/ diff %lu\n", stop.tv_nsec, stop.tv_nsec - start.tv_nsec);
    goto end;

    cleanup:
    for (int i = 0; i < numThreads; ++i) {
        if (threads[i].handle) {
            threadWaitForExit(&threads[i]);
            threadClose(&threads[i]);
        }
    }

    end:
    return result;
}

int main(int argc, char **argv) {
    consoleInit(NULL);

    threadSuspendTest();

    while (appletMainLoop()) {
        hidScanInput();
        u64 kdown = hidKeysDown(CONTROLLER_P1_AUTO);
        if (kdown & KEY_PLUS)
            break;
    }

    consoleExit(NULL);
    return 0;
}
