#pragma once

#include "main.h"
#include "bcc/bcc.h"
#include "bcc/bcc_utils.h"
#include <vector>

struct SafetyLimits_t
{
    double OVERVOLTAGE_LIMIT;
    double UNDERVOLTAGE_LIMIT;
    double OVERTEMPERATURE_LIMIT;
    double UNDERTEMPERATURE_LIMIT;
    double CHARGE_CURRENT_LIMIT;
    double DISCHARGE_CURRENT_LIMIT;
    uint32_t COMMUNICATION_TIMEOUT; // In millis
};

struct UserSettings_t
{
    std::vector<BCC::Config_t> SLAVE_CONFIG;
    SafetyLimits_t SAFETY_LIMITS;
    double SHUNT_RESISTANCE;
    double NTC_RESISTANCE;
    double NTC_BETA;
    double BATTERY_AMPHOURS;

    // Current and SoC
    bool INVERT_CURRENT;                 // Inverts the current measurement, and therefore also the SoC calculation. Usefull in case sense wires are connected the wrong way
    bool AUTO_CALIBRATE_SOC;             // Automatically calibrates the SoC at Vmax
    double AUTO_CALIBRATE_SOC_THRESHOLD; // The threshold for the SoC to be calibrated to 100%, relative to max pack voltage (0-1) [V]

    // Balancing
    double MIN_BALANCING_VOLTAGE;      // [V] The minimum voltage for a cell to be considered for balancing
    double MIN_BALANCING_DIFF_VOLTAGE; // [V] The minimum voltage difference between two cells for balancing to be considered
    bool IMPROVED_BALANCING_ACCURACY;  // If true, the balancing trigger time is shorter, decreasing undershoots but possbily increasing TPL bus bandwidth

    // CAN IDs
    uint32_t CAN_MEAS1_ID;
    uint32_t CAN_MEAS2_ID;
    uint32_t CAN_BCC_DIAG_ID;
    uint32_t CAN_BMS_STATE_ID;
    uint32_t CAN_PCC_ID;
    uint32_t CAN_CHARGING_ID;

    // Timing
    uint32_t BMS_MAIN_LOOP_PERIOD;          // [ms] The minimum time that the main SlaveController loop takes.
    uint32_t BMS_MEASUREMENT_PERIOD_FACTOR; // Determines how many x loops the measurements are fetched.
    uint32_t CAN_MEASUREMENT_MSG_PERIOD_FACTOR;
    uint32_t CAN_BMS_STATE_MSG_PERIOD_FACTOR;
    uint32_t MINIMUM_FAULT_ACTIVE_TIME; // [ms] The minimum time that a fault is active in milliseconds
    uint32_t CAN_CHARGING_MSG_PERIOD;   // [ms] The period at which the BMS sends CAN messages to the charger
    uint32_t CAN_PCC_PERIOD; // [ms] The period at which the BMS sends CAN messages about the PCC
};

const UserSettings_t DEFAULT_SETTINGS = {
    .SLAVE_CONFIG = {
        {.DEVICE_TYPE = BCC_DEVICE_MC33771C, .CELL_COUNT = 12, .NTC_COUNT = 7},
        {.DEVICE_TYPE = BCC_DEVICE_MC33771C, .CELL_COUNT = 12, .NTC_COUNT = 7},
        {.DEVICE_TYPE = BCC_DEVICE_MC33771C, .CELL_COUNT = 12, .NTC_COUNT = 7},
        {.DEVICE_TYPE = BCC_DEVICE_MC33771C, .CELL_COUNT = 12, .NTC_COUNT = 7},
        {.DEVICE_TYPE = BCC_DEVICE_MC33771C, .CELL_COUNT = 12, .NTC_COUNT = 7},
        {.DEVICE_TYPE = BCC_DEVICE_MC33771C, .CELL_COUNT = 12, .NTC_COUNT = 7},
        {.DEVICE_TYPE = BCC_DEVICE_MC33771C, .CELL_COUNT = 12, .NTC_COUNT = 7},
        {.DEVICE_TYPE = BCC_DEVICE_MC33771C, .CELL_COUNT = 12, .NTC_COUNT = 7},
        {.DEVICE_TYPE = BCC_DEVICE_MC33771C, .CELL_COUNT = 12, .NTC_COUNT = 7},
        {.DEVICE_TYPE = BCC_DEVICE_MC33771C, .CELL_COUNT = 12, .NTC_COUNT = 7},
        {.DEVICE_TYPE = BCC_DEVICE_MC33771C, .CELL_COUNT = 12, .NTC_COUNT = 7},
        {.DEVICE_TYPE = BCC_DEVICE_MC33771C, .CELL_COUNT = 12, .NTC_COUNT = 7},
        {.DEVICE_TYPE = BCC_DEVICE_MC33771C, .CELL_COUNT = 10, .NTC_COUNT = 7},
        {.DEVICE_TYPE = BCC_DEVICE_MC33771C, .CELL_COUNT = 12, .NTC_COUNT = 7},
        // {.DEVICE_TYPE = BCC_DEVICE_MC33771C, .CELL_COUNT = 12, .NTC_COUNT = 0},
        // {.DEVICE_TYPE = BCC_DEVICE_MC33771C, .CELL_COUNT = 12, .NTC_COUNT = 0},
        // {.DEVICE_TYPE = BCC_DEVICE_MC33771C, .CELL_COUNT = 12, .NTC_COUNT = 7},
        // {.DEVICE_TYPE = BCC_DEVICE_MC33771C, .CELL_COUNT = 12, .NTC_COUNT = 7},
        // {.DEVICE_TYPE = BCC_DEVICE_MC33771C, .CELL_COUNT = 12, .NTC_COUNT = 0},
        // {.DEVICE_TYPE = BCC_DEVICE_MC33771C, .CELL_COUNT = 12, .NTC_COUNT = 7},
        // {.DEVICE_TYPE = BCC_DEVICE_MC33771C, .CELL_COUNT = 12, .NTC_COUNT = 7},
        // {.DEVICE_TYPE = BCC_DEVICE_MC33771C, .CELL_COUNT = 12, .NTC_COUNT = 7},
        // {.DEVICE_TYPE = BCC_DEVICE_MC33771C, .CELL_COUNT = 12, .NTC_COUNT = 7},
        // {.DEVICE_TYPE = BCC_DEVICE_MC33771C, .CELL_COUNT = 12, .NTC_COUNT = 7},

        // {.DEVICE_TYPE = BCC_DEVICE_MC33771C, .CELL_COUNT = 14, .NTC_COUNT = 7, .CURRENT_SENSING_ENABLED = true, .AMPHOUR_BACKUP_REG = 4},
        // {.DEVICE_TYPE = BCC_DEVICE_MC33771C, .CELL_COUNT = 12, .NTC_COUNT = 0}
    },
    .SAFETY_LIMITS = {.OVERVOLTAGE_LIMIT = 3.6, .UNDERVOLTAGE_LIMIT = 2.2, .OVERTEMPERATURE_LIMIT = 60, .UNDERTEMPERATURE_LIMIT = 5, .CHARGE_CURRENT_LIMIT = 50, .DISCHARGE_CURRENT_LIMIT = 100, .COMMUNICATION_TIMEOUT = 500},
    //.SHUNT_RESISTANCE = 1.5e-3 * 0.9892315863523707, // Shunt resistance of test batteries (calibrated)
    .SHUNT_RESISTANCE = 100e-6, // Shunt resistance of Nova, 100 microOhm
    .NTC_RESISTANCE = 10000,
    .NTC_BETA = 3977,
    .BATTERY_AMPHOURS = 5.2,

    // Current and SoC
    .INVERT_CURRENT = true,
    .AUTO_CALIBRATE_SOC = true,
    .AUTO_CALIBRATE_SOC_THRESHOLD = 0.96,

    // Balancing
    .MIN_BALANCING_VOLTAGE = 3.3,
    .MIN_BALANCING_DIFF_VOLTAGE = 0.02,
    .IMPROVED_BALANCING_ACCURACY = true,    

    // CAN ID's
    .CAN_MEAS1_ID = 0x610,
    .CAN_MEAS2_ID = 0x611,
    .CAN_BCC_DIAG_ID = 0x612,
    .CAN_BMS_STATE_ID = 0x613,
    .CAN_PCC_ID = 0x614,
    .CAN_CHARGING_ID = 0x1806E5F4,

    // Timing
    .BMS_MAIN_LOOP_PERIOD = 50,
    .BMS_MEASUREMENT_PERIOD_FACTOR = 5,
    .CAN_MEASUREMENT_MSG_PERIOD_FACTOR = 5,
    .CAN_BMS_STATE_MSG_PERIOD_FACTOR = 5,
    .MINIMUM_FAULT_ACTIVE_TIME = 2000,
    .CAN_CHARGING_MSG_PERIOD = 400,
    .CAN_PCC_PERIOD = 250, // [ms] The period at which the BMS sends CAN messages about the PCC

};

/*******************************************************************************
 * Initial register values for the BCC. Shouldn't have to change these
 *******************************************************************************/

#define BMS_BAL_RESISTANCE (37.5 + 3.3 + 0.8) // [Ohm] The balancing resistance on the BMS slave board
#define MC33771C_AVG_CURRENT_DRAW 9e-3        // [A] The average current draw of the MC33771C in normal operation
#define CURRENT_NOISE_THRESHOLD 15e-3         // [A] The threshold for which current is considered noise (and therefore set to zero)

#define MC33771C_SYS_CFG1_VALUE(isense) (                                                                                                  \
    MC33771C_SYS_CFG1_CYCLIC_TIMER(MC33771C_SYS_CFG1_CYCLIC_TIMER_DISABLED_ENUM_VAL) |                                                     \
    MC33771C_SYS_CFG1_DIAG_TIMEOUT(MC33771C_SYS_CFG1_DIAG_TIMEOUT_1S_ENUM_VAL) |                                                           \
    MC33771C_SYS_CFG1_I_MEAS_EN((isense ? MC33771C_SYS_CFG1_I_MEAS_EN_ENABLED_ENUM_VAL : MC33771C_SYS_CFG1_I_MEAS_EN_DISABLED_ENUM_VAL)) | \
    MC33771C_SYS_CFG1_CB_DRVEN(MC33771C_SYS_CFG1_CB_DRVEN_DISABLED_ENUM_VAL) |                                                             \
    MC33771C_SYS_CFG1_GO2DIAG(MC33771C_SYS_CFG1_GO2DIAG_EXIT_ENUM_VAL) |                                                                   \
    MC33771C_SYS_CFG1_CB_MANUAL_PAUSE(MC33771C_SYS_CFG1_CB_MANUAL_PAUSE_DISABLED_ENUM_VAL) |                                               \
    MC33771C_SYS_CFG1_SOFT_RST(MC33771C_SYS_CFG1_SOFT_RST_DISABLED_ENUM_VAL) |                                                             \
    MC33771C_SYS_CFG1_FAULT_WAVE(MC33771C_SYS_CFG1_FAULT_WAVE_DISABLED_ENUM_VAL) |                                                         \
    MC33771C_SYS_CFG1_WAVE_DC_BITX(MC33771C_SYS_CFG1_WAVE_DC_BITX_500US_ENUM_VAL))

/* Initial value of SYS_CFG2 register. */
#define MC33771C_SYS_CFG2_VALUE (                                                                   \
    MC33771C_SYS_CFG2_FLT_RST_CFG(MC33771C_SYS_CFG2_FLT_RST_CFG_COM_RESET_OSC_MON_RESET_ENUM_VAL) | \
    MC33771C_SYS_CFG2_TIMEOUT_COMM(MC33771C_SYS_CFG2_TIMEOUT_COMM_256MS_ENUM_VAL) |                 \
    MC33771C_SYS_CFG2_NUMB_ODD(MC33771C_SYS_CFG2_NUMB_ODD_EVEN_ENUM_VAL) |                          \
    MC33771C_SYS_CFG2_HAMM_ENCOD(MC33771C_SYS_CFG2_HAMM_ENCOD_DECODE_ENUM_VAL))

/* Initial value of ADC_CFG register.
 * AVG:          No averaging
 * PGA_GAIN:     Auto
 * ADC1_A_DEF:   16-bit
 * ADC1_B_DEF:   16-bit
 * ADC2_DEF:     16-bit
 * CC_RST:       Reset
 */

#define BCC_ADC_AVG BCC_AVG_8
#define MC33771C_ADC_CFG_VALUE (                                               \
    MC33771C_ADC_CFG_AVG(BCC_ADC_AVG) |                                        \
    MC33771C_ADC_CFG_PGA_GAIN(MC33771C_ADC_CFG_PGA_GAIN_AUTO_ENUM_VAL) |       \
    MC33771C_ADC_CFG_ADC1_A_DEF(MC33771C_ADC_CFG_ADC1_A_DEF_16_BIT_ENUM_VAL) | \
    MC33771C_ADC_CFG_ADC1_B_DEF(MC33771C_ADC_CFG_ADC1_B_DEF_16_BIT_ENUM_VAL) | \
    MC33771C_ADC_CFG_ADC2_DEF(MC33771C_ADC_CFG_ADC2_DEF_16_BIT_ENUM_VAL) |     \
    MC33771C_ADC_CFG_CC_RST(MC33771C_ADC_CFG_CC_RST_RESET_ENUM_VAL))

/* Initial value of ADC2_OFFSET_COMP register.
 * CC_RST_CFG:        Don't reset CC on read
 * FREE_CNT:          Roll over
 * ALLCBOFF_ON_SHORT: Only shorted CB's are turned off
 * ADC2_OFFSET_COMP:  0
 */
#define MC33771C_ADC2_OFFSET_COMP_VALUE (                                                                   \
    MC33771C_ADC2_OFFSET_COMP_CC_RST_CFG(MC33771C_ADC2_OFFSET_COMP_CC_RST_CFG_NO_ACTION_ENUM_VAL) |         \
    MC33771C_ADC2_OFFSET_COMP_FREE_CNT(MC33771C_ADC2_OFFSET_COMP_FREE_CNT_ROLL_OVER_ENUM_VAL) |             \
    MC33771C_ADC2_OFFSET_COMP_ALLCBOFFONSHORT(MC33771C_ADC2_OFFSET_COMP_ALLCBOFFONSHORT_SHORTED_ENUM_VAL) | \
    MC33771C_ADC2_OFFSET_COMP_ADC2_OFFSET_COMP(BCC_GET_ADC2_OFFSET(0)))

/* Initial value of GPIO_CFG1 register.
 * Both MC33771C and MC33772C EVBs expect NTC at all GPIOx pins.
 */
#define MC33771C_GPIO_CFG1_VALUE (                                                     \
    MC33771C_GPIO_CFG1_GPIO0_CFG(MC33771C_GPIO_CFG1_GPIO0_CFG_ANALOG_RATIO_ENUM_VAL) | \
    MC33771C_GPIO_CFG1_GPIO1_CFG(MC33771C_GPIO_CFG1_GPIO1_CFG_ANALOG_RATIO_ENUM_VAL) | \
    MC33771C_GPIO_CFG1_GPIO2_CFG(MC33771C_GPIO_CFG1_GPIO2_CFG_ANALOG_RATIO_ENUM_VAL) | \
    MC33771C_GPIO_CFG1_GPIO3_CFG(MC33771C_GPIO_CFG1_GPIO3_CFG_ANALOG_RATIO_ENUM_VAL) | \
    MC33771C_GPIO_CFG1_GPIO4_CFG(MC33771C_GPIO_CFG1_GPIO4_CFG_ANALOG_RATIO_ENUM_VAL) | \
    MC33771C_GPIO_CFG1_GPIO5_CFG(MC33771C_GPIO_CFG1_GPIO5_CFG_ANALOG_RATIO_ENUM_VAL) | \
    MC33771C_GPIO_CFG1_GPIO6_CFG(MC33771C_GPIO_CFG1_GPIO6_CFG_ANALOG_RATIO_ENUM_VAL))

/* Initial value of GPIO_CFG2 register. */
#define MC33771C_GPIO_CFG2_VALUE (                                                                                                                          \
    MC33771C_GPIO_CFG2_GPIO2_SOC(MC33771C_GPIO_CFG2_GPIO2_SOC_ADC_TRG_DISABLED_ENUM_VAL) |                                                                  \
    MC33771C_GPIO_CFG2_GPIO0_WU(MC33771C_GPIO_CFG2_GPIO0_WU_NO_WAKEUP_ENUM_VAL) |                                                                           \
    MC33771C_GPIO_CFG2_GPIO0_FLT_ACT(MC33771C_GPIO_CFG2_GPIO0_FLT_ACT_DISABLED_ENUM_VAL) | /* Note: GPIOx_DR are initialized to zero (low output level). */ \
    MC33771C_GPIO_CFG2_GPIO6_DR(MC33771C_GPIO_CFG2_GPIO6_DR_LOW_ENUM_VAL) |                                                                                 \
    MC33771C_GPIO_CFG2_GPIO5_DR(MC33771C_GPIO_CFG2_GPIO5_DR_LOW_ENUM_VAL) |                                                                                 \
    MC33771C_GPIO_CFG2_GPIO4_DR(MC33771C_GPIO_CFG2_GPIO4_DR_LOW_ENUM_VAL) |                                                                                 \
    MC33771C_GPIO_CFG2_GPIO3_DR(MC33771C_GPIO_CFG2_GPIO3_DR_LOW_ENUM_VAL) |                                                                                 \
    MC33771C_GPIO_CFG2_GPIO2_DR(MC33771C_GPIO_CFG2_GPIO2_DR_LOW_ENUM_VAL) |                                                                                 \
    MC33771C_GPIO_CFG2_GPIO1_DR(MC33771C_GPIO_CFG2_GPIO1_DR_LOW_ENUM_VAL) |                                                                                 \
    MC33771C_GPIO_CFG2_GPIO0_DR(MC33771C_GPIO_CFG2_GPIO0_DR_LOW_ENUM_VAL))

/* Initial value of FAULT_MASK1 register. */
#define MC33771C_FAULT_MASK1_VALUE (                                                                               \
    MC33771C_FAULT_MASK1_VPWR_OV_FLT_MASK_12_F(MC33771C_FAULT_MASK1_VPWR_OV_FLT_MASK_12_F_NOT_MASKED_ENUM_VAL) |   \
    MC33771C_FAULT_MASK1_VPWR_LV_FLT_MASK_11_F(MC33771C_FAULT_MASK1_VPWR_LV_FLT_MASK_11_F_NOT_MASKED_ENUM_VAL) |   \
    MC33771C_FAULT_MASK1_COM_LOSS_FLT_MASK_10_F(MC33771C_FAULT_MASK1_COM_LOSS_FLT_MASK_10_F_NOT_MASKED_ENUM_VAL) | \
    MC33771C_FAULT_MASK1_COM_ERR_FLT_MASK_9_F(MC33771C_FAULT_MASK1_COM_ERR_FLT_MASK_9_F_NOT_MASKED_ENUM_VAL) |     \
    MC33771C_FAULT_MASK1_CSB_WUP_FLT_MASK_8_F(MC33771C_FAULT_MASK1_CSB_WUP_FLT_MASK_8_F_NOT_MASKED_ENUM_VAL) |     \
    MC33771C_FAULT_MASK1_GPIO0_WUP_FLT_MASK_7_F(MC33771C_FAULT_MASK1_GPIO0_WUP_FLT_MASK_7_F_NOT_MASKED_ENUM_VAL) | \
    MC33771C_FAULT_MASK1_I2C_ERR_FLT_MASK_6_F(MC33771C_FAULT_MASK1_I2C_ERR_FLT_MASK_6_F_NOT_MASKED_ENUM_VAL) |     \
    MC33771C_FAULT_MASK1_IS_OL_FLT_MASK_5_F(MC33771C_FAULT_MASK1_IS_OL_FLT_MASK_5_F_NOT_MASKED_ENUM_VAL) |         \
    MC33771C_FAULT_MASK1_IS_OC_FLT_MASK_4_F(MC33771C_FAULT_MASK1_IS_OC_FLT_MASK_4_F_NOT_MASKED_ENUM_VAL) |         \
    MC33771C_FAULT_MASK1_AN_OT_FLT_MASK_3_F(MC33771C_FAULT_MASK1_AN_OT_FLT_MASK_3_F_NOT_MASKED_ENUM_VAL) |         \
    MC33771C_FAULT_MASK1_AN_UT_FLT_MASK_2_F(MC33771C_FAULT_MASK1_AN_UT_FLT_MASK_2_F_NOT_MASKED_ENUM_VAL) |         \
    MC33771C_FAULT_MASK1_CT_OV_FLT_MASK_1_F(MC33771C_FAULT_MASK1_CT_OV_FLT_MASK_1_F_NOT_MASKED_ENUM_VAL) |         \
    MC33771C_FAULT_MASK1_CT_UV_FLT_MASK_0_F(MC33771C_FAULT_MASK1_CT_UV_FLT_MASK_0_F_NOT_MASKED_ENUM_VAL))

/* Initial value of FAULT_MASK2 register. */
#define MC33771C_FAULT_MASK2_VALUE (                                                                                 \
    MC33771C_FAULT_MASK2_VCOM_OV_FLT_MASK_15_F(MC33771C_FAULT_MASK2_VCOM_OV_FLT_MASK_15_F_NOT_MASKED_ENUM_VAL) |     \
    MC33771C_FAULT_MASK2_VCOM_UV_FLT_MASK_14_F(MC33771C_FAULT_MASK2_VCOM_UV_FLT_MASK_14_F_NOT_MASKED_ENUM_VAL) |     \
    MC33771C_FAULT_MASK2_VANA_OV_FLT_MASK_13_F(MC33771C_FAULT_MASK2_VANA_OV_FLT_MASK_13_F_NOT_MASKED_ENUM_VAL) |     \
    MC33771C_FAULT_MASK2_VANA_UV_FLT_MASK_12_F(MC33771C_FAULT_MASK2_VANA_UV_FLT_MASK_12_F_NOT_MASKED_ENUM_VAL) |     \
    MC33771C_FAULT_MASK2_ADC1_B_FLT_MASK_11_F(MC33771C_FAULT_MASK2_ADC1_B_FLT_MASK_11_F_NOT_MASKED_ENUM_VAL) |       \
    MC33771C_FAULT_MASK2_ADC1_A_FLT_MASK_10_F(MC33771C_FAULT_MASK2_ADC1_A_FLT_MASK_10_F_NOT_MASKED_ENUM_VAL) |       \
    MC33771C_FAULT_MASK2_GND_LOSS_FLT_MASK_9_F(MC33771C_FAULT_MASK2_GND_LOSS_FLT_MASK_9_F_NOT_MASKED_ENUM_VAL) |     \
    MC33771C_FAULT_MASK2_AN_OPEN_FLT_MASK_6_F(MC33771C_FAULT_MASK2_AN_OPEN_FLT_MASK_6_F_NOT_MASKED_ENUM_VAL) |       \
    MC33771C_FAULT_MASK2_GPIO_SHORT_FLT_MASK_5_F(MC33771C_FAULT_MASK2_GPIO_SHORT_FLT_MASK_5_F_NOT_MASKED_ENUM_VAL) | \
    MC33771C_FAULT_MASK2_CB_SHORT_FLT_MASK_4_F(MC33771C_FAULT_MASK2_CB_SHORT_FLT_MASK_4_F_NOT_MASKED_ENUM_VAL) |     \
    MC33771C_FAULT_MASK2_CB_OPEN_FLT_MASK_3_F(MC33771C_FAULT_MASK2_CB_OPEN_FLT_MASK_3_F_NOT_MASKED_ENUM_VAL) |       \
    MC33771C_FAULT_MASK2_OSC_ERR_FLT_MASK_2_F(MC33771C_FAULT_MASK2_OSC_ERR_FLT_MASK_2_F_NOT_MASKED_ENUM_VAL) |       \
    MC33771C_FAULT_MASK2_DED_ERR_FLT_MASK_1_F(MC33771C_FAULT_MASK2_DED_ERR_FLT_MASK_1_F_NOT_MASKED_ENUM_VAL) |       \
    MC33771C_FAULT_MASK2_FUSE_ERR_FLT_MASK_0_F(MC33771C_FAULT_MASK2_FUSE_ERR_FLT_MASK_0_F_NOT_MASKED_ENUM_VAL))

/* Initial value of FAULT_MASK3 register. */
#define MC33771C_FAULT_MASK3_VALUE (                                                                         \
    MC33771C_FAULT_MASK3_CC_OVR_FLT_MASK_15_F(MC33771C_FAULT_MASK3_CC_OVR_FLT_MASK_15_F_MASKED_ENUM_VAL) |   \
    MC33771C_FAULT_MASK3_DIAG_TO_FLT_MASK_14_F(MC33771C_FAULT_MASK3_DIAG_TO_FLT_MASK_14_F_MASKED_ENUM_VAL) | \
    MC33771C_FAULT_MASK3_EOT_CB14_MASK_13_F(MC33771C_FAULT_MASK3_EOT_CB14_MASK_13_F_MASKED_ENUM_VAL) |       \
    MC33771C_FAULT_MASK3_EOT_CB13_MASK_12_F(MC33771C_FAULT_MASK3_EOT_CB13_MASK_12_F_MASKED_ENUM_VAL) |       \
    MC33771C_FAULT_MASK3_EOT_CB12_MASK_11_F(MC33771C_FAULT_MASK3_EOT_CB12_MASK_11_F_MASKED_ENUM_VAL) |       \
    MC33771C_FAULT_MASK3_EOT_CB11_MASK_10_F(MC33771C_FAULT_MASK3_EOT_CB11_MASK_10_F_MASKED_ENUM_VAL) |       \
    MC33771C_FAULT_MASK3_EOT_CB10_MASK_9_F(MC33771C_FAULT_MASK3_EOT_CB10_MASK_9_F_MASKED_ENUM_VAL) |         \
    MC33771C_FAULT_MASK3_EOT_CB9_MASK_8_F(MC33771C_FAULT_MASK3_EOT_CB9_MASK_8_F_MASKED_ENUM_VAL) |           \
    MC33771C_FAULT_MASK3_EOT_CB8_MASK_7_F(MC33771C_FAULT_MASK3_EOT_CB8_MASK_7_F_MASKED_ENUM_VAL) |           \
    MC33771C_FAULT_MASK3_EOT_CB7_MASK_6_F(MC33771C_FAULT_MASK3_EOT_CB7_MASK_6_F_MASKED_ENUM_VAL) |           \
    MC33771C_FAULT_MASK3_EOT_CB6_MASK_5_F(MC33771C_FAULT_MASK3_EOT_CB6_MASK_5_F_MASKED_ENUM_VAL) |           \
    MC33771C_FAULT_MASK3_EOT_CB5_MASK_4_F(MC33771C_FAULT_MASK3_EOT_CB5_MASK_4_F_MASKED_ENUM_VAL) |           \
    MC33771C_FAULT_MASK3_EOT_CB4_MASK_3_F(MC33771C_FAULT_MASK3_EOT_CB4_MASK_3_F_MASKED_ENUM_VAL) |           \
    MC33771C_FAULT_MASK3_EOT_CB3_MASK_2_F(MC33771C_FAULT_MASK3_EOT_CB3_MASK_2_F_MASKED_ENUM_VAL) |           \
    MC33771C_FAULT_MASK3_EOT_CB2_MASK_1_F(MC33771C_FAULT_MASK3_EOT_CB2_MASK_1_F_MASKED_ENUM_VAL) |           \
    MC33771C_FAULT_MASK3_EOT_CB1_MASK_0_F(MC33771C_FAULT_MASK3_EOT_CB1_MASK_0_F_MASKED_ENUM_VAL))

/* Initial value of TH_ALL_CT register. */
#define MC33771C_TH_ALL_CT_VALUE(ov_mv, uv_mv) (                                                                                      \
    MC33771C_TH_ALL_CT_ALL_CT_OV_TH(BCC_GET_TH_CTX(ov_mv)) /* CT OV threshold. It is enabled/disabled through OV_UV_EN register. */ | \
    MC33771C_TH_ALL_CT_ALL_CT_UV_TH(BCC_GET_TH_CTX(uv_mv)) /* CT UV threshold. It is enabled/disabled through OV_UV_EN register. */   \
)

/* Initial value of TH_ANx_OT registers. */
#define MC33771C_TH_ANX_OT_VALUE(an_reg_val) (                                                                          \
    MC33771C_TH_AN1_OT_AN_OT_TH(an_reg_val) /* AN OT threshold. It is enabled/disabled through FAULT_MASK1 register. */ \
)

/* Initial value of TH_ANx_UT registers. */
#define MC33771C_TH_ANX_UT_VALUE(an_reg_val) (                                                                          \
    MC33771C_TH_AN1_UT_AN_UT_TH(an_reg_val) /* AN UT threshold. It is enabled/disabled through FAULT_MASK1 register. */ \
)
