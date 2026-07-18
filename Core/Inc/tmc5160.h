/**
  ******************************************************************************
  * @file    tmc5160.h
  * @brief   Minimal TMC5160 SPI driver (motion controller + stepper driver).
  *
  * Wiring (see main.h):
  *   SPI1 (PA5/PA6/PA7)  ->  SCK / SDO / SDI
  *   PC7  (TMC_CSN)      ->  CSN   (software chip select, active low)
  *   PB6  (TMC_DRV_ENN)  ->  DRV_ENN (driver stage enable, active low)
  ******************************************************************************
  */

#ifndef TMC5160_H
#define TMC5160_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/* Register addresses (TMC5160 datasheet, chapter 6) */
#define TMC5160_REG_GCONF         0x00
#define TMC5160_REG_GSTAT         0x01
#define TMC5160_REG_IOIN          0x04
#define TMC5160_REG_GLOBALSCALER  0x0B
#define TMC5160_REG_IHOLD_IRUN    0x10
#define TMC5160_REG_TPOWERDOWN    0x11
#define TMC5160_REG_TSTEP         0x12
#define TMC5160_REG_TPWMTHRS      0x13
#define TMC5160_REG_RAMPMODE      0x20
#define TMC5160_REG_XACTUAL       0x21
#define TMC5160_REG_VACTUAL       0x22
#define TMC5160_REG_VSTART        0x23
#define TMC5160_REG_A1            0x24
#define TMC5160_REG_V1            0x25
#define TMC5160_REG_AMAX          0x26
#define TMC5160_REG_VMAX          0x27
#define TMC5160_REG_DMAX          0x28
#define TMC5160_REG_D1            0x2A
#define TMC5160_REG_VSTOP         0x2B
#define TMC5160_REG_XTARGET       0x2D
#define TMC5160_REG_RAMP_STAT     0x35
#define TMC5160_REG_CHOPCONF      0x6C
#define TMC5160_REG_DRV_STATUS    0x6F
#define TMC5160_REG_PWMCONF       0x70

/* RAMPMODE values */
#define TMC5160_RAMPMODE_POSITION  0u  /* move to XTARGET using the ramp   */
#define TMC5160_RAMPMODE_VEL_POS   1u  /* velocity mode, positive VMAX     */
#define TMC5160_RAMPMODE_VEL_NEG   2u  /* velocity mode, negative VMAX     */
#define TMC5160_RAMPMODE_HOLD      3u  /* hold current velocity            */

/* RAMP_STAT bits */
#define TMC5160_RAMP_STAT_POSITION_REACHED  (1u << 9)

/* SPI status byte (first byte clocked out with every datagram) */
#define TMC5160_SPI_STATUS_RESET_FLAG    (1u << 0)
#define TMC5160_SPI_STATUS_DRIVER_ERROR  (1u << 1)
#define TMC5160_SPI_STATUS_STANDSTILL    (1u << 3)

/* 1 full rotation of a 1.8 deg motor at 256 microsteps */
#define TMC5160_USTEPS_PER_REV  51200

/* Write a 32-bit value to a register; returns the SPI status byte. */
uint8_t  tmc5160_write(uint8_t reg, uint32_t value);

/* Read a 32-bit register (issues the two datagrams a read requires). */
uint32_t tmc5160_read(uint8_t reg);

/* Verify SPI communication and load a basic SpreadCycle + ramp config.
 * Returns false if the chip does not answer (wrong wiring / no power). */
bool tmc5160_init(void);

/* Drive DRV_ENN: true = power stage on, false = motor freewheels. */
void tmc5160_set_driver_enabled(bool enable);

/* Start a positioning move to an absolute target (microsteps). */
void tmc5160_move_to(int32_t xtarget);

/* True once XACTUAL == XTARGET and the ramp has finished. */
bool tmc5160_position_reached(void);

#ifdef __cplusplus
}
#endif

#endif /* TMC5160_H */
