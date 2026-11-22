#define _GNU_SOURCE

#include <assert.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "roadcast_client.h"

int main(void) {
    char socket_path[128];
    snprintf(socket_path, sizeof(socket_path), "/tmp/roadcast-buckets-e2e-%d.sock",
             (int)getpid());
    unlink(socket_path);

    pid_t child = fork();
    assert(child >= 0);
    if (child == 0) {
        execl("./build/roadcastd", "roadcastd", "--fake", "--hz", "60",
              "--socket", socket_path, (char *)NULL);
        _exit(127);
    }

    roadcast_client_t *client = NULL;
    int err = 0;
    for (int i = 0; i < 40; i++) {
        struct timespec ts = {.tv_sec = 0, .tv_nsec = 50000000L};
        nanosleep(&ts, NULL);
        client = roadcast_client_connect(socket_path, &err);
        if (client)
            break;
    }
    assert(client != NULL);
    assert((roadcast_client_capabilities(client) &
            ROADCAST_CAP_SIGNAL_BUCKETS) != 0);

    /* Let daemon integrate ~300 ms at 60 Hz. */
    struct timespec wait_ts = {.tv_sec = 0, .tv_nsec = 300000000L};
    nanosleep(&wait_ts, NULL);

    roadcast_signal_bucket_t buckets[64];
    size_t count = 0;
    uint64_t latest_id = 0;
    int rc = roadcast_client_read_buckets(client, 0, UINT32_MAX, buckets, 64,
                                          &count, &latest_id);
    assert(rc == ROADCAST_CLIENT_OK);
    assert(count >= 1);
    assert(latest_id == buckets[count - 1].bucket_id);

    int found_covered = 0;
    for (size_t i = 0; i < count; i++) {
        if (buckets[i].covered_ms > 0)
            found_covered = 1;
    }
    assert(found_covered == 1);

    /* Single-signal query returns only that signal. */
    uint32_t target_sig = buckets[0].signal_index;
    roadcast_signal_bucket_t single[16];
    size_t single_count = 0;
    rc = roadcast_client_read_buckets(client, 0, target_sig, single, 16,
                                      &single_count, &latest_id);
    assert(rc == ROADCAST_CLIENT_OK);
    assert(single_count >= 1);
    for (size_t i = 0; i < single_count; i++) {
        assert(single[i].signal_index == target_sig);
    }

    roadcast_client_close(client);
    kill(child, SIGTERM);
    int status = 0;
    waitpid(child, &status, 0);
    unlink(socket_path);
    printf("bucket e2e tests passed (channels=%zu, latest_bucket=%llu)\n",
           count, (unsigned long long)latest_id);
    return 0;
}
