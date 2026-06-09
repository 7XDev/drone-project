/**
 * @file    flycm.c
 * @brief   Fly up ~0.5 m, then forward a user-specified distance in cm.
 *
 * Uses a single gentle forward nudge with no aggressive braking.
 * The drone's own drag and stabiliser stop it naturally.
 *
 * Usage:
 *   ./examples/flycm [distance_cm]
 *
 * Sequence:
 *   1. Connect & arm
 *   2. Take off to ~0.5 m (thrust ramp)
 *   3. Fly forward by the specified centimetres
 *   4. Land & disarm
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../lib/crazydrone.h"

/* ── Flight parameters ──────────────────────────────────────────────────── */

/** Hover thrust that produces roughly 0.5 m altitude (tune for your drone). */
#define FLYCM_HOVER_THRUST      38000

/** Ramp time (ms) for takeoff. */
#define FLYCM_TAKEOFF_RAMP_MS   1500

/** Ramp time (ms) for landing. */
#define FLYCM_LAND_RAMP_MS      2000

/**
 * Forward pitch angle (degrees).
 * Very small — just enough to barely drift forward.
 * The drone's own drag will stop it once pitch returns to 0.
 */
#define FLYCM_FORWARD_PITCH_DEG 0.3f

/**
 * Duration (ms) of the forward nudge.
 * Short burst to minimise momentum buildup.
 */
#define FLYCM_FORWARD_MS        10

/**
 * Settle time (ms) after the forward move — let drag kill momentum.
 */
#define FLYCM_SETTLE_MS         200

/**
 * Estimated distance (cm) moved per nudge.
 * Used for multi-nudge cycles on larger distances.
 */
#define FLYCM_CM_PER_NUDGE      0.3f

/* ── Main ───────────────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    int distance_cm = 0;

    /* ── Parse / prompt for distance ────────────────────────────────────── */
    if (argc >= 2) {
        distance_cm = atoi(argv[1]);
        if (distance_cm <= 0) {
            fprintf(stderr, "Error: distance must be > 0 cm\n");
            return EXIT_FAILURE;
        }
    } else {
        printf("Enter distance to fly forward (cm, > 0): ");
        fflush(stdout);
        char buf[32];
        if (!fgets(buf, sizeof(buf), stdin)) {
            fprintf(stderr, "Failed to read input.\n");
            return EXIT_FAILURE;
        }
        distance_cm = atoi(buf);
        if (distance_cm <= 0) {
            fprintf(stderr, "Error: distance must be > 0 cm\n");
            return EXIT_FAILURE;
        }
    }

    /* Calculate nudges needed */
    int nudges = (int)ceilf(distance_cm / FLYCM_CM_PER_NUDGE);
    if (nudges < 1) nudges = 1;

    /* ── Banner ─────────────────────────────────────────────────────────── */
    printf("\n");
    printf("╔══════════════════════════════════════════════╗\n");
    printf("║              FlyCM — Forward Flight          ║\n");
    printf("╠══════════════════════════════════════════════╣\n");
    printf("║  Distance:  %5d cm                           ║\n", distance_cm);
    printf("║  Nudges:    %5d (%.1f cm/nudge)              ║\n",
           nudges, FLYCM_CM_PER_NUDGE);
    printf("║  Pitch:     %.1f° for %d ms                  ║\n",
           FLYCM_FORWARD_PITCH_DEG, FLYCM_FORWARD_MS);
    printf("║  Takeoff:   ~0.5 m                           ║\n");
    printf("╚══════════════════════════════════════════════╝\n\n");

    /* ── Create handle ─────────────────────────────────────────────────── */
    CrazyDrone *drone = cd_create(CD_DEFAULT_IP, CD_DEFAULT_PORT);
    if (!drone) {
        fprintf(stderr, "Failed to create drone handle.\n");
        return EXIT_FAILURE;
    }

    cd_print_banner("FlyCM", drone->ip, drone->port);

    /* ── Connect ───────────────────────────────────────────────────────── */
    printf("Connecting to drone...\n");
    int connected = cd_connect(drone, 5000);
    if (!connected) {
        printf("Warning: No reply from drone. Continuing anyway.\n");
        printf("(Make sure you're connected to the drone's WiFi AP)\n\n");
    }

    /* NORMAL speed — slow & predictable */
    cd_set_speed_mode(drone, CD_SPEED_NORMAL);

    /* ── Arm ────────────────────────────────────────────────────────────── */
    printf("\nArming...\n");
    cd_arm(drone, 1000);
    cd_sleep_ms(300);

    /* ── Takeoff: ramp thrust up to hover level (~0.5 m) ───────────────── */
    printf("Taking off to ~0.5 m...\n");
    cd_takeoff(drone, FLYCM_HOVER_THRUST, FLYCM_TAKEOFF_RAMP_MS);

    /* Settle at hover altitude */
    cd_hover(drone, 0.0f, 0.0f, 0.0f, FLYCM_HOVER_THRUST, 400);

    /* ── Fly forward using gentle nudges ───────────────────────────────── */
    printf("Flying forward %d cm (%d nudges)...\n", distance_cm, nudges);

    for (int i = 0; i < nudges; i++) {
        /* Forward nudge: tiny pitch for a brief moment */
        cd_hover(drone,
                 0.0f,
                 -FLYCM_FORWARD_PITCH_DEG,
                 0.0f,
                 FLYCM_HOVER_THRUST,
                 FLYCM_FORWARD_MS);

        /* Let drag stop the drone — no reverse pitch, just hover */
        cd_hover(drone, 0.0f, 0.0f, 0.0f, FLYCM_HOVER_THRUST, FLYCM_SETTLE_MS);

        printf("\r  Progress: %d / %d nudges", i + 1, nudges);
        fflush(stdout);
    }
    printf("\n");

    /* Final stabilise */
    cd_hover(drone, 0.0f, 0.0f, 0.0f, FLYCM_HOVER_THRUST, 400);

    /* ── Land ────────────────────────────────────────────────────────────── */
    printf("Landing...\n");
    cd_land(drone, FLYCM_HOVER_THRUST, FLYCM_LAND_RAMP_MS);

    cd_sleep_ms(300);
    cd_emergency_stop(drone);

    printf("\n✓ Mission complete! Flew forward %d cm.\n", distance_cm);

    cd_destroy(drone);
    return EXIT_SUCCESS;
}