/*
 * Smart Boiler HMI. Receives status frames from the controller (or the
 * simulator) over UDP and sends operator commands back.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "boiler_app.h"
#include "boiler_screen.h"
#include "sp_display.h"
#include "sp_frame.h"
#include "sp_snapshot.h"
#include "sp_udp.h"

#define DEFAULT_LISTEN_PORT 47301
#define DEFAULT_PEER_PORT   47302
#define LOOP_SLEEP_US       4000

typedef struct {
    SpDisplayConfig display;
    const char *host;
    uint16_t listen_port;
    uint16_t peer_port;
    bool sim_tools;
    bool show_sim_panel;
    BoilerChartRange range;
    const char *snapshot_path;
    uint32_t snapshot_at_ms; /* controller uptime that triggers the snapshot */
    uint32_t timeout_s;      /* wall-clock limit, 0 = none */
    const char *record_dir;  /* write frame_NNNN.ppm files here */
    uint32_t record_frames;
    uint32_t record_every_ms;
} Options;

typedef struct {
    SpUdp udp;
    uint32_t sequence;
} Link;

static void usage(const char *program)
{
    fprintf(stderr,
            "usage: %s [--headless | --fbdev] [--sim-tools] [--host ADDR]\n"
            "          [--listen-port N] [--peer-port N] [--range 2m|10m|1h]\n"
            "          [--snapshot FILE.ppm --snapshot-at-ms UPTIME] [--timeout-s N]\n"
            "          [--show-sim-panel]\n"
            "          [--record DIR --record-frames N --record-every-ms MS]\n",
            program);
}

static bool parse_range(const char *text, BoilerChartRange *range)
{
    if (strcmp(text, "2m") == 0) {
        *range = BOILER_CHART_2_MIN;
    } else if (strcmp(text, "10m") == 0) {
        *range = BOILER_CHART_10_MIN;
    } else if (strcmp(text, "1h") == 0) {
        *range = BOILER_CHART_60_MIN;
    } else {
        return false;
    }
    return true;
}

static bool parse_options(int argc, char **argv, Options *options)
{
    *options = (Options){
        .display = {.kind = SP_DISPLAY_SDL,
                    .width = BOILER_SCREEN_WIDTH,
                    .height = BOILER_SCREEN_HEIGHT,
                    .title = "Sparrow - Smart Boiler"},
        .host = "127.0.0.1",
        .listen_port = DEFAULT_LISTEN_PORT,
        .peer_port = DEFAULT_PEER_PORT,
        .range = BOILER_CHART_10_MIN,
        .record_frames = 100,
        .record_every_ms = 100,
    };
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        const char *value = i + 1 < argc ? argv[i + 1] : NULL;

        if (strcmp(arg, "--headless") == 0) {
            options->display.kind = SP_DISPLAY_HEADLESS;
        } else if (strcmp(arg, "--fbdev") == 0) {
            options->display.kind = SP_DISPLAY_FBDEV;
        } else if (strcmp(arg, "--sim-tools") == 0) {
            options->sim_tools = true;
        } else if (strcmp(arg, "--show-sim-panel") == 0) {
            options->show_sim_panel = true;
        } else if (value == NULL) {
            return false;
        } else if (strcmp(arg, "--host") == 0) {
            options->host = value, i++;
        } else if (strcmp(arg, "--listen-port") == 0) {
            options->listen_port = (uint16_t)atoi(value), i++;
        } else if (strcmp(arg, "--peer-port") == 0) {
            options->peer_port = (uint16_t)atoi(value), i++;
        } else if (strcmp(arg, "--range") == 0) {
            if (!parse_range(value, &options->range)) {
                return false;
            }
            i++;
        } else if (strcmp(arg, "--snapshot") == 0) {
            options->snapshot_path = value, i++;
        } else if (strcmp(arg, "--snapshot-at-ms") == 0) {
            options->snapshot_at_ms = (uint32_t)strtoul(value, NULL, 10), i++;
        } else if (strcmp(arg, "--record") == 0) {
            options->record_dir = value, i++;
        } else if (strcmp(arg, "--record-frames") == 0) {
            options->record_frames = (uint32_t)strtoul(value, NULL, 10), i++;
        } else if (strcmp(arg, "--record-every-ms") == 0) {
            options->record_every_ms = (uint32_t)strtoul(value, NULL, 10), i++;
        } else if (strcmp(arg, "--timeout-s") == 0) {
            options->timeout_s = (uint32_t)atoi(value), i++;
        } else {
            return false;
        }
    }
    return true;
}

static void send_frame(Link *link, BoilerFrameKind kind, const void *payload, size_t size)
{
    uint8_t buffer[SP_FRAME_MAX_SIZE];
    const size_t length =
        sp_frame_encode(buffer, sizeof buffer, (uint16_t)kind, ++link->sequence, payload, size);

    if (length > 0) {
        sp_udp_send(&link->udp, buffer, length);
    }
}

static void send_command(void *context, const BoilerCommands *commands)
{
    send_frame(context, BOILER_FRAME_COMMANDS, commands, sizeof *commands);
}

static void send_injection(void *context, const BoilerFaultInjection *injection)
{
    send_frame(context, BOILER_FRAME_FAULT_INJECTION, injection, sizeof *injection);
}

static void drain_link(Link *link, BoilerApp *app)
{
    uint8_t buffer[SP_FRAME_MAX_SIZE];
    size_t size;

    while ((size = sp_udp_receive(&link->udp, buffer, sizeof buffer)) > 0) {
        SpFrameHeader header;
        const void *payload;

        if (sp_frame_decode(buffer, size, &header, &payload) == SP_OK &&
            header.kind == BOILER_FRAME_STATUS && header.payload_size == sizeof(BoilerStatus)) {
            BoilerStatus status;
            memcpy(&status, payload, sizeof status);
            boiler_app_ingest(app, &status, sp_tick_ms());
        }
    }
}

static int write_snapshot(const Options *options)
{
    lv_timer_handler();
    lv_refr_now(NULL);
    if (sp_snapshot_write_ppm(lv_screen_active(), options->snapshot_path) != SP_OK) {
        fprintf(stderr, "cannot write %s\n", options->snapshot_path);
        return 1;
    }
    return 0;
}

/* Writes the next frame of a recording. Returns false on an I/O error. */
static bool record_frame(const Options *options, uint32_t index)
{
    char path[512];

    snprintf(path, sizeof path, "%s/frame_%04u.ppm", options->record_dir, (unsigned)index);
    lv_refr_now(NULL);
    if (sp_snapshot_write_ppm(lv_screen_active(), path) != SP_OK) {
        fprintf(stderr, "cannot write %s\n", path);
        return false;
    }
    return true;
}

static BoilerApp app;

int main(int argc, char **argv)
{
    Options options;
    Link link = {0};
    BoilerScreenHost host = {send_command, send_injection, &link};
    BoilerScreen *screen;
    const uint32_t started_ms = sp_tick_ms();
    uint32_t recorded = 0;
    uint32_t next_frame_ms = 0;

    if (!parse_options(argc, argv, &options)) {
        usage(argv[0]);
        return 2;
    }
    if (sp_display_init(&options.display) != SP_OK) {
        fprintf(stderr, "display backend not available\n");
        return 1;
    }
    if (sp_udp_open(&link.udp, options.host, options.listen_port, options.peer_port) != SP_OK) {
        fprintf(stderr, "cannot open UDP port %u\n", options.listen_port);
        return 1;
    }

    boiler_app_init(&app);
    screen = boiler_screen_create(lv_screen_active(), &app, &host, options.sim_tools);
    boiler_screen_set_range(screen, options.range);
    boiler_screen_show_sim_panel(screen, options.show_sim_panel);

    for (;;) {
        uint32_t now;

        drain_link(&link, &app);
        now = sp_tick_ms();
        boiler_app_tick(&app, now);
        boiler_screen_update(screen);
        lv_timer_handler();

        if (options.snapshot_path != NULL && app.have_status &&
            app.status.uptime_ms >= options.snapshot_at_ms) {
            return write_snapshot(&options);
        }
        if (options.record_dir != NULL && app.have_status && now >= next_frame_ms) {
            if (!record_frame(&options, recorded)) {
                return 1;
            }
            next_frame_ms = now + options.record_every_ms;
            if (++recorded == options.record_frames) {
                return 0;
            }
        }
        if (options.timeout_s != 0 && now - started_ms > options.timeout_s * 1000u) {
            fprintf(stderr, "timeout waiting for the controller\n");
            return 3;
        }
        usleep(LOOP_SLEEP_US);
    }
}
