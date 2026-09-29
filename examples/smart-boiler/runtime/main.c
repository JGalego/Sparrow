/*
 * Smart Boiler controller runtime: runs boiler_step() at a fixed period
 * against a BoilerIo backend, publishes status to the HMI and accepts its
 * commands. This is the process that runs on the target.
 */
#define _GNU_SOURCE
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "boiler_commands.h"
#include "boiler_controller.h"
#include "boiler_io_udp.h"
#include "sp_frame.h"
#include "sp_period.h"
#include "sp_udp.h"
#include "sp_watchdog.h"

#define HMI_LISTEN_PORT   47302
#define HMI_PORT          47301
#define PLANT_LISTEN_PORT 47311
#define PLANT_PORT        47312
#define DEFAULT_PERIOD_MS 100
#define DEFAULT_STALE_MS  500
#define MAX_STEP_PERIODS  10

typedef struct {
    const char *host;
    const char *hmi_host;
    const char *plant_host;
    uint32_t period_ms;
    uint32_t stale_ms;
    double speed; /* simulated seconds per second; 1 on a real plant */
    const char *watchdog;
    uint32_t run_s; /* 0 = until SIGINT/SIGTERM */
    uint16_t hmi_listen_port;
    uint16_t hmi_port;
    uint16_t plant_listen_port;
    uint16_t plant_port;
} Options;

typedef struct {
    SpUdp udp;
    uint32_t sequence;
} HmiLink;

static volatile sig_atomic_t stop_requested;

static void on_signal(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}

static void usage(const char *program)
{
    fprintf(stderr,
            "usage: %s [--bind ADDR] [--hmi-host ADDR] [--plant-host ADDR] [--period-ms N]\n"
            "          [--stale-ms N] [--speed X] [--watchdog DEVICE] [--run-s N]\n"
            "          [--hmi-listen-port N] [--hmi-port N] [--plant-listen-port N]\n"
            "          [--plant-port N]\n",
            program);
}

static bool parse_options(int argc, char **argv, Options *options)
{
    *options = (Options){.host = "127.0.0.1",
                         .hmi_host = "127.0.0.1",
                         .plant_host = "127.0.0.1",
                         .period_ms = DEFAULT_PERIOD_MS,
                         .stale_ms = DEFAULT_STALE_MS,
                         .speed = 1.0,
                         .hmi_listen_port = HMI_LISTEN_PORT,
                         .hmi_port = HMI_PORT,
                         .plant_listen_port = PLANT_LISTEN_PORT,
                         .plant_port = PLANT_PORT};
    for (int i = 1; i + 1 < argc; i += 2) {
        const char *arg = argv[i];
        const char *value = argv[i + 1];

        if (strcmp(arg, "--bind") == 0) {
            options->host = value;
        } else if (strcmp(arg, "--hmi-host") == 0) {
            options->hmi_host = value;
        } else if (strcmp(arg, "--plant-host") == 0) {
            options->plant_host = value;
        } else if (strcmp(arg, "--period-ms") == 0) {
            options->period_ms = (uint32_t)strtoul(value, NULL, 10);
        } else if (strcmp(arg, "--stale-ms") == 0) {
            options->stale_ms = (uint32_t)strtoul(value, NULL, 10);
        } else if (strcmp(arg, "--speed") == 0) {
            options->speed = strtod(value, NULL);
        } else if (strcmp(arg, "--watchdog") == 0) {
            options->watchdog = value;
        } else if (strcmp(arg, "--run-s") == 0) {
            options->run_s = (uint32_t)strtoul(value, NULL, 10);
        } else if (strcmp(arg, "--hmi-listen-port") == 0) {
            options->hmi_listen_port = (uint16_t)strtoul(value, NULL, 10);
        } else if (strcmp(arg, "--hmi-port") == 0) {
            options->hmi_port = (uint16_t)strtoul(value, NULL, 10);
        } else if (strcmp(arg, "--plant-listen-port") == 0) {
            options->plant_listen_port = (uint16_t)strtoul(value, NULL, 10);
        } else if (strcmp(arg, "--plant-port") == 0) {
            options->plant_port = (uint16_t)strtoul(value, NULL, 10);
        } else {
            return false;
        }
    }
    return argc % 2 == 1 && options->period_ms > 0 && options->speed > 0.0;
}

static void drain_hmi(HmiLink *hmi, BoilerIoUdp *io, BoilerCommands *pending)
{
    uint8_t buffer[SP_FRAME_MAX_SIZE];
    size_t size;

    while ((size = sp_udp_receive(&hmi->udp, buffer, sizeof buffer)) > 0) {
        SpFrameHeader header;
        const void *payload;

        if (sp_frame_decode(buffer, size, &header, &payload) != SP_OK) {
            continue;
        }
        if (header.kind == BOILER_FRAME_COMMANDS && header.payload_size == sizeof(BoilerCommands)) {
            BoilerCommands received;
            memcpy(&received, payload, sizeof received);
            boiler_commands_merge(pending, &received);
        } else if (header.kind == BOILER_FRAME_FAULT_INJECTION) {
            boiler_io_udp_forward(io, buffer, size);
        }
    }
}

static void publish_status(HmiLink *hmi, const BoilerController *controller)
{
    BoilerStatus status;
    uint8_t buffer[SP_FRAME_MAX_SIZE];
    size_t size;

    boiler_get_status(controller, &status);
    size = sp_frame_encode(buffer, sizeof buffer, BOILER_FRAME_STATUS, ++hmi->sequence, &status,
                           sizeof status);
    if (size > 0) {
        sp_udp_send(&hmi->udp, buffer, size);
    }
}

/* Controller time for one cycle: measured wall time, scaled, and bounded after a stall. */
static uint32_t step_ms(const Options *options, uint32_t elapsed_ms)
{
    const uint32_t limit = options->period_ms * MAX_STEP_PERIODS;
    uint32_t scaled = (uint32_t)((double)elapsed_ms * options->speed + 0.5);

    if (scaled == 0) {
        scaled = 1;
    }
    return scaled > limit ? limit : scaled;
}

static int run(const Options *options, BoilerController *controller, BoilerIo io, HmiLink *hmi,
               BoilerIoUdp *plant, SpWatchdog *watchdog)
{
    SpPeriod period;
    BoilerCommands pending = {0};
    bool io_ready = false; /* no control step before the first fresh inputs */
    const uint64_t end_ns =
        options->run_s ? sp_monotonic_ns() + (uint64_t)options->run_s * 1000000000u : 0;

    sp_period_start(&period, (uint32_t)(options->period_ms * 1000.0 / options->speed));
    while (!stop_requested && (end_ns == 0 || sp_monotonic_ns() < end_ns)) {
        BoilerInputs inputs;
        BoilerOutputs outputs = boiler_io_safe_outputs();
        const uint32_t dt_ms = step_ms(options, sp_period_wait(&period));
        const bool fresh = io.read(io.context, &inputs);

        drain_hmi(hmi, plant, &pending);
        if (fresh && !io_ready) {
            io_ready = true;
            fprintf(stderr, "field inputs available, control started\n");
        }
        if (io_ready) {
            boiler_step(controller, &inputs, &pending, dt_ms, &outputs);
            pending = (BoilerCommands){0};
        }
        io.write(io.context, &outputs);
        publish_status(hmi, controller);
        sp_watchdog_kick(watchdog);
    }
    {
        const BoilerOutputs safe = boiler_io_safe_outputs();
        io.write(io.context, &safe);
    }
    fprintf(stderr, "stopped: %u overruns, %u stale input reads\n", (unsigned)period.overruns,
            (unsigned)plant->stale_reads);
    return 0;
}

static BoilerController controller;

int main(int argc, char **argv)
{
    Options options;
    const BoilerConfig config = boiler_config_default();
    const char *reason = NULL;
    BoilerIoUdp plant;
    HmiLink hmi = {0};
    SpWatchdog watchdog = {-1};
    int result;

    if (!parse_options(argc, argv, &options)) {
        usage(argv[0]);
        return 2;
    }
    if (boiler_init(&controller, &config, &reason) != SP_OK) {
        fprintf(stderr, "invalid configuration: %s\n", reason);
        return 2;
    }
    if (boiler_io_udp_open(&plant, options.host, options.plant_listen_port, options.plant_port,
                           options.stale_ms) != SP_OK ||
        sp_udp_open(&hmi.udp, options.host, options.hmi_listen_port, options.hmi_port) != SP_OK) {
        fprintf(stderr, "cannot open UDP ports\n");
        return 1;
    }
    if (sp_udp_set_peer(&plant.udp, options.plant_host, options.plant_port) != SP_OK ||
        sp_udp_set_peer(&hmi.udp, options.hmi_host, options.hmi_port) != SP_OK) {
        fprintf(stderr, "invalid host address\n");
        return 2;
    }
    if (options.watchdog != NULL && sp_watchdog_open(&watchdog, options.watchdog) != SP_OK) {
        perror(options.watchdog);
        return 1;
    }
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    result = run(&options, &controller, boiler_io_udp_interface(&plant), &hmi, &plant, &watchdog);
    sp_watchdog_close(&watchdog);
    boiler_io_udp_close(&plant);
    sp_udp_close(&hmi.udp);
    return result;
}
