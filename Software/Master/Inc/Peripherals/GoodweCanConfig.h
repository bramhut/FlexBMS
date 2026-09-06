#pragma once

/*
 * GoodWe CAN protocol selection.
 *
 * Candidate A is the reverse-engineered GoodWe HV profile. Candidate B is the
 * SMA/Pylontech-style profile used by some GoodWe installations.
 */
#define GOODWE_CAN_PROTOCOL_A 1U
#define GOODWE_CAN_PROTOCOL_B 2U

#ifndef GOODWE_CAN_PROTOCOL
#define GOODWE_CAN_PROTOCOL GOODWE_CAN_PROTOCOL_A
#endif

#if GOODWE_CAN_PROTOCOL != GOODWE_CAN_PROTOCOL_A && GOODWE_CAN_PROTOCOL != GOODWE_CAN_PROTOCOL_B
#error "GOODWE_CAN_PROTOCOL must select GOODWE_CAN_PROTOCOL_A or GOODWE_CAN_PROTOCOL_B"
#endif

/* Application-level replies remain disabled until the inverter proves one is required. */
#ifndef GOODWE_CAN_ENABLE_APPLICATION_RESPONSES
#define GOODWE_CAN_ENABLE_APPLICATION_RESPONSES 0U
#endif

#if GOODWE_CAN_ENABLE_APPLICATION_RESPONSES > 1U
#error "GOODWE_CAN_ENABLE_APPLICATION_RESPONSES must be 0 or 1"
#endif

/* Candidate A experimental frames. Their meanings are not confirmed. */
#ifndef GOODWE_CAN_A_ENABLE_45A
#define GOODWE_CAN_A_ENABLE_45A 0U
#endif

#ifndef GOODWE_CAN_A_ENABLE_460
#define GOODWE_CAN_A_ENABLE_460 0U
#endif

/* Candidate B frame 0x354 is not emitted until its payload is confirmed. */
#ifndef GOODWE_CAN_B_ENABLE_354
#define GOODWE_CAN_B_ENABLE_354 0U
#endif

#if GOODWE_CAN_A_ENABLE_45A > 1U || GOODWE_CAN_A_ENABLE_460 > 1U || GOODWE_CAN_B_ENABLE_354 > 1U
#error "GoodWe CAN optional frame switches must be 0 or 1"
#endif

/* Current pack: 96 cells at approximately 3.2 V gives six 48 V-equivalent modules. */
#ifndef GOODWE_CAN_A_MODULE_COUNT
#define GOODWE_CAN_A_MODULE_COUNT 6U
#endif

/* No measured SoH exists yet; this is deliberately configurable and documented. */
#ifndef GOODWE_CAN_SOH_PERCENT
#define GOODWE_CAN_SOH_PERCENT 100U
#endif

/* SOC advertised to the inverter until the coulomb counter is calibrated. */
#ifndef GOODWE_CAN_UNAVAILABLE_SOC_PERCENT
#define GOODWE_CAN_UNAVAILABLE_SOC_PERCENT 30U
#endif

#if GOODWE_CAN_A_MODULE_COUNT > 0xFFFFU
#error "GOODWE_CAN_A_MODULE_COUNT does not fit the protocol field"
#endif

#if GOODWE_CAN_SOH_PERCENT > 100U
#error "GOODWE_CAN_SOH_PERCENT must be between 0 and 100"
#endif

#if GOODWE_CAN_UNAVAILABLE_SOC_PERCENT > 100U
#error "GOODWE_CAN_UNAVAILABLE_SOC_PERCENT must be between 0 and 100"
#endif

#if GOODWE_CAN_PROTOCOL == GOODWE_CAN_PROTOCOL_A
#define GOODWE_CAN_BITRATE 250000U
#define GOODWE_CAN_NOMINAL_PRESCALER 32U
#else
#define GOODWE_CAN_BITRATE 500000U
#define GOODWE_CAN_NOMINAL_PRESCALER 16U
#endif

#define GOODWE_CAN_NOMINAL_SYNC_JUMP_WIDTH 2U
#define GOODWE_CAN_NOMINAL_TIME_SEGMENT_1 13U
#define GOODWE_CAN_NOMINAL_TIME_SEGMENT_2 2U
#define GOODWE_CAN_PERIOD_MS 1000U
