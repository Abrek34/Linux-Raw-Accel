/*
 * virtmouse_game.c — P57: inject synthetic game-speed mouse motion into a
 * uinput virtual device so the running rawaccel daemon hot-plugs it, grabs
 * it, and populates its per-device lat histogram at game speeds.
 *
 * Quick run (P64, after P57):
 *   gcc -O2 -o build-manual/virtmouse-game scripts/virtmouse-game.c
 *   sudo build-manual/virtmouse-game pan 10     # run as root/input group
 *   rawaccel-cli latency                        # read the per-device histogram
 *
 * Scenarios (counts/sec):
 *   flick     : very fast short bursts (one-shot big deltas) — RTS/hero shooter flicks
 *   pan       : sustained high-speed horizontal panning (4000 cnt/s sustained)
 *   mix       : blend of flick + slow precise micro-moves (idle/precision)
 *   precision : P94 "precision flick" — 1 s sawtooth ramping 120→4000 cnt/s
 *               (≈150→5000 ips at 800 DPI), crossing the esport grid
 *               landmarks 2000/3000/4000 ips each sweep, exercising the
 *               precision band (120–900) and fast-flick (>4000 cnt/s)
 *               curve regions in one run.
 *   locked    : P109 "lock window" — coordinate stream confined to a
 *               90×60 px box, exactly like a cursor grab-locked in a small
 *               test window. At a steady 1000 Hz the simulated cursor is
 *               advanced at game speed; on box-edge contact it re-wraps to
 *               the opposite wall (pointer reload) so coordinate updates
 *               NEVER leave the box. The re-wrap instant injects a large
 *               single-tick corrective delta — the "wrap-induced spike"
 *               Aj7 watches for in the latency histogram.
 *
 * The daemon's is_physical_mouse() ignores devices whose phys contains
 * "uinput" or whose name ends "(RawAccel)" — so we must NOT use those
 * markers if we want the daemon to grab this device.
 *
 * Usage: virtmouse_game <scenario> <duration_sec>
 *   scenario: flick | pan | mix | precision | locked
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <time.h>

static long now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000L + ts.tv_nsec / 1000L;
}

static void sleep_us(long us) {
    struct timespec ts = { 0, us * 1000L };
    while (nanosleep(&ts, &ts) < 0 && errno == EINTR) {}
}

static int create_mouse(const char* name) {
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) { perror("open /dev/uinput"); return -1; }

    struct uinput_setup setup;
    memset(&setup, 0, sizeof(setup));
    strncpy(setup.name, name, UINPUT_MAX_NAME_SIZE - 1);
    setup.id.bustype = BUS_USB;
    setup.id.vendor  = 0x0e0f;   /* VMware vendor, like real VMMouse */
    setup.id.product = 0x1337;
    setup.id.version = 1;
    /* phys must NOT contain "uinput" for the daemon to grab it */
    if (ioctl(fd, UI_SET_PHYS, "usb:0e0f:1337:") < 0)
        perror("UI_SET_PHYS (warn only)");

    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_KEYBIT, BTN_LEFT);
    ioctl(fd, UI_SET_KEYBIT, BTN_RIGHT);
    ioctl(fd, UI_SET_KEYBIT, BTN_MIDDLE);
    ioctl(fd, UI_SET_EVBIT, EV_REL);
    ioctl(fd, UI_SET_RELBIT, REL_X);
    ioctl(fd, UI_SET_RELBIT, REL_Y);
    ioctl(fd, UI_SET_RELBIT, REL_WHEEL);

    if (ioctl(fd, UI_DEV_SETUP, &setup) < 0) { perror("UI_DEV_SETUP"); close(fd); return -1; }
    if (ioctl(fd, UI_DEV_CREATE) < 0) { perror("UI_DEV_CREATE"); close(fd); return -1; }
    return fd;
}

static void emit(int fd, unsigned type, unsigned code, int val) {
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = type; ev.code = code; ev.value = val;
    ev.time.tv_sec = 0; ev.time.tv_usec = 0;
    if (write(fd, &ev, sizeof(ev)) < 0) { /* ignore */ }
}

static void emit_rel(int fd, int dx) {
    if (dx != 0) emit(fd, EV_REL, REL_X, dx);
    emit(fd, EV_SYN, SYN_REPORT, 0);
}

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s <flick|pan|mix|precision|locked> <duration_sec>\n", argv[0]);
        return 2;
    }
    const char* scenario = argv[1];
    int duration = atoi(argv[2]);
    if (duration <= 0) duration = 5;

    int fd = create_mouse("P57 GameSpeed Test Mouse");
    if (fd < 0) return 1;
    printf("[virtmouse] created virtual mouse, scenario=%s duration=%ds\n",
           scenario, duration);
    fflush(stdout);
    sleep(2); /* let the daemon hot-plug + grab the new node */

    long t0 = now_us();
    long end = t0 + duration * 1000000L;
    int pan_sign = 1;
    long emit_time = t0; /* precision: next count's scheduled deadline */
    long next_click = t0 + 200000; /* first click 200 ms in */

    /* locked: simulated cursor confined to a 90x60 px box. lpx/lpy is the
     * logical cursor, lox/loy the accumulated emitted position (emission
     * keeps the fractional part so the average rate stays exact). */
    const double LOCK_W = 90.0, LOCK_H = 60.0;
    double lpx = LOCK_W / 2, lpy = LOCK_H / 2;
    double lox = lpx, loy = lpy;
    double lvx = 4.0, lvy = 2.0;      /* px/tick at 1000 Hz (4000/2000 cnt/s) */
    long   lnext = 0;                 /* next 1000 Hz tick deadline */
    long   lwraps = 0;                /* box-edge re-wrap (pointer reload) count */

    while (1) {
        long now = now_us();
        if (now >= end) break;

        if (strcmp(scenario, "flick") == 0) {
            /* 10 ms bursts of large deltas (~4000 cnt/s peak) separated by idle */
            emit_rel(fd, 25);          /* 25 cnt in 10 ms ≈ 2500 cnt/s */
            sleep_us(10000);
        } else if (strcmp(scenario, "pan") == 0) {
            /* sustained 4 cnt / 1 ms ≈ 4000 cnt/s, alternating direction */
            emit_rel(fd, pan_sign * 4);
            pan_sign = -pan_sign;
            sleep_us(1000);
        } else if (strcmp(scenario, "precision") == 0) {
            /* P94 (P64 harness genişletme): "precision flick" — 1 s sawtooth
             * that sweeps the injected rate 120→4000 cnt/s (≈150→5000 ips at
             * 800 DPI) and back, crossing the esport grid landmarks
             * 2000/3000/4000 ips (1600/2400/3200 cnt/s at 800 DPI) every
             * sweep. Slow end emits one count per tick, so the precision band
             * (120–900 cnt/s) injects real micro-motion instead of idling.
             *
             * Deadline-driven (P101 fix): every count has a scheduled deadline
             * emit_time, advanced by 1e6/rate(emit_time) so the injected count
             * density tracks the intended sawtooth rate(t) = 120 + 3880·phase
             * EXACTLY. If this loop wakes late (loaded VM, scheduler jitter) it
             * catches up by emitting the counts whose deadlines have passed.
             * The previous "emit 1, then sleep 1e6/rate" scheme under-delivered
             * the high end (measured ~2400 cnt/s instead of 4000 under load)
             * because per-iteration overhead stacked on every sleep. */
            int burst = 0;
            while (emit_time <= now) {
                double phase = (double)((emit_time - t0) % 1000000L) / 1000000.0;
                double rate  = 120.0 + 3880.0 * phase;   /* cnt/s at this deadline */
                if (rate > 4000.0) rate = 4000.0;        /* clamp sawtooth top */
                emit_rel(fd, 1);
                emit_time += (long)(1000000.0 / rate);   /* next count deadline */
                if (++burst > 16) break;                 /* safety bound per wake */
            }
            sleep_us(500);
        } else if (strcmp(scenario, "locked") == 0) {
            /* P109: a pointer grab-locked in a small test window. Coordinate
             * updates stay inside the 90x60 px box: the cursor advances at a
             * steady 1000 Hz rate and, on box-edge contact, re-wraps to the
             * opposite wall (pointer reload). The re-wrap tick emits a single
             * large corrective delta — the move a confined cursor "gives back"
             * when it runs out of box. Continuous small deltas + periodic
             * box-width sign-flip jumps = the real locked-cursor signature we
             * want the daemon's hot path to chew on. */
            if (lnext == 0) lnext = t0;
            int lburst = 0;
            while (lnext <= now && lburst < 32) {
                lpx += lvx;
                lpy += lvy;
                if (lpx >= LOCK_W) { lpx -= LOCK_W; lwraps++; }
                else if (lpx < 0.0) { lpx += LOCK_W; lwraps++; }
                if (lpy >= LOCK_H) { lpy -= LOCK_H; lwraps++; }
                else if (lpy < 0.0) { lpy += LOCK_H; lwraps++; }
                double ix = lpx - lox, iy = lpy - loy;
                lox = lpx - (ix - (long)ix);
                loy = lpy - (iy - (long)iy);
                long dx = (long)ix, dy = (long)iy;
                if (dx != 0) emit(fd, EV_REL, REL_X, (int)dx);
                if (dy != 0) emit(fd, EV_REL, REL_Y, (int)dy);
                emit(fd, EV_SYN, SYN_REPORT, 0);
                lnext += 1000;
                if (++lburst >= 32) break;
            }
            sleep_us(500);
        } else { /* mix */
            /* heavy flick burst then slow precise micro-moves */
            emit_rel(fd, 20);
            sleep_us(5000);
            emit_rel(fd, 1);
            sleep_us(15000);
            emit_rel(fd, 1);
            sleep_us(15000);
        }

        /* fire a BTN_LEFT click every 200 ms — measures click overhead/jitter
         * on the same hot path (button events are forwarded 1:1 via uinput) */
        long now2 = now_us();
        if (now2 >= next_click) {
            emit(fd, EV_KEY, BTN_LEFT, 1);
            emit(fd, EV_SYN, SYN_REPORT, 0);
            emit(fd, EV_KEY, BTN_LEFT, 0);
            emit(fd, EV_SYN, SYN_REPORT, 0);
            next_click = now2 + 200000;
        }
    }

    sleep(1); /* allow drain */
    ioctl(fd, UI_DEV_DESTROY);
    close(fd);
    printf("[virtmouse] done. injected %ld s of %s motion.\n",
           (now_us() - t0) / 1000000L, scenario);
    if (strcmp(scenario, "locked") == 0)
        printf("[virtmouse] locked: box 90x60 px, 1000 Hz, %ld box-edge re-wraps "
               "(pointer reloads).\n", lwraps);
    return 0;
}
