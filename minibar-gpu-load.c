#define _GNU_SOURCE
#include <errno.h>
#include <inttypes.h>
#include <linux/perf_event.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <unistd.h>

/* i915 render-engine busy time, in nanoseconds. No per-process scanning. */
struct sample { uint64_t busy, enabled; };

static int sample(int fd, struct sample *s)
{
    ssize_t n;
    do { n = read(fd, s, sizeof(*s)); } while (n < 0 && errno == EINTR);
    if (n == sizeof(*s)) return 0;
    fprintf(stderr, "minibar-gpu-load: cannot read counter\n");
    return -1;
}

int main(void)
{
    unsigned int type;
    int cpu;
    uint64_t config;
    FILE *f = fopen("/sys/bus/event_source/devices/i915/type", "r");
    if (!f || fscanf(f, "%u", &type) != 1) goto unavailable;
    fclose(f);
    f = fopen("/sys/bus/event_source/devices/i915/events/rcs0-busy", "r");
    if (!f || fscanf(f, "config=%" SCNx64, &config) != 1) goto unavailable;
    fclose(f);
    f = fopen("/sys/bus/event_source/devices/i915/cpumask", "r");
    if (!f || fscanf(f, "%d", &cpu) != 1) goto unavailable;
    fclose(f);

    struct perf_event_attr attr = {
        .size = sizeof(attr), .type = type, .config = config,
        .read_format = PERF_FORMAT_TOTAL_TIME_ENABLED,
    };
    int fd = syscall(SYS_perf_event_open, &attr, -1, cpu, -1, PERF_FLAG_FD_CLOEXEC);
    if (fd < 0) {
        perror("minibar-gpu-load: perf_event_open (requires CAP_PERFMON)");
        return EXIT_FAILURE;
    }
    struct sample prev, cur;
    if (sample(fd, &prev)) return EXIT_FAILURE;
    /* The bar holds stdin open. Exit when its pipe closes, even after a crash. */
    struct pollfd parent = { .fd = STDIN_FILENO, .events = POLLIN };
    for (;;) {
        int ready = poll(&parent, 1, 1000);
        if (ready < 0 && errno == EINTR) continue;
        if (ready < 0) return EXIT_FAILURE;
        if (ready > 0) break;
        if (sample(fd, &cur)) return EXIT_FAILURE;
        if (cur.enabled > prev.enabled && cur.busy >= prev.busy) {
            double pct = 100.0 * (cur.busy - prev.busy) / (cur.enabled - prev.enabled);
            printf("%.0f\n", pct > 100.0 ? 100.0 : pct);
        } else {
            puts("?");
        }
        if (fflush(stdout) == EOF) break;
        prev = cur;
    }
    close(fd);
    return EXIT_SUCCESS;
unavailable:
    if (f) fclose(f);
    fprintf(stderr, "minibar-gpu-load: i915 render busy counter unavailable\n");
    return EXIT_FAILURE;
}
