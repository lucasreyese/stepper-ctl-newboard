/**
  ******************************************************************************
  * @file    tmc5160.c
  * @brief   Minimal TMC5160 SPI driver.
  *
  * The TMC5160 uses 40-bit SPI datagrams (SPI mode 3, MSB first):
  *   byte 0      : register address (bit 7 set = write)
  *   bytes 1..4  : 32-bit data, MSB first
  * Every transfer also returns 40 bits: an 8-bit SPI status followed by the
  * 32-bit data requested by the *previous* datagram, so a register read takes
  * two transfers.
  ******************************************************************************
  */

#include "tmc5160.h"
#include "main.h"

extern SPI_HandleTypeDef hspi1;

static uint8_t tmc5160_xfer(uint8_t reg, uint32_t data, uint32_t *reply)
{
  uint8_t tx[5] = {
    reg,
    (uint8_t)(data >> 24),
    (uint8_t)(data >> 16),
    (uint8_t)(data >> 8),
    (uint8_t)(data),
  };
  uint8_t rx[5] = {0};

  HAL_GPIO_WritePin(TMC_CSN_GPIO_Port, TMC_CSN_Pin, GPIO_PIN_RESET);
  HAL_SPI_TransmitReceive(&hspi1, tx, rx, sizeof(tx), HAL_MAX_DELAY);
  HAL_GPIO_WritePin(TMC_CSN_GPIO_Port, TMC_CSN_Pin, GPIO_PIN_SET);

  if (reply != NULL)
  {
    *reply = ((uint32_t)rx[1] << 24) | ((uint32_t)rx[2] << 16) |
             ((uint32_t)rx[3] << 8)  |  (uint32_t)rx[4];
  }
  return rx[0];
}

uint8_t tmc5160_write(uint8_t reg, uint32_t value)
{
  return tmc5160_xfer(reg | 0x80u, value, NULL);
}

uint32_t tmc5160_read(uint8_t reg)
{
  uint32_t value = 0;

  tmc5160_xfer(reg, 0, NULL);       /* select register            */
  tmc5160_xfer(reg, 0, &value);     /* clock its contents back out */
  return value;
}

void tmc5160_set_driver_enabled(bool enable)
{
  /* DRV_ENN is active low */
  HAL_GPIO_WritePin(TMC_DRV_ENN_GPIO_Port, TMC_DRV_ENN_Pin,
                    enable ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

bool tmc5160_init(void)
{
  /* IOIN bits 31:24 hold the chip version: 0x30 for the TMC5160.
   * 0x00 or 0xFF means MISO is dead (wiring, power, or CSN problem). */
  uint32_t ioin = tmc5160_read(TMC5160_REG_IOIN);
  if (((ioin >> 24) & 0xFFu) != 0x30u)
  {
    return false;
  }

  /* Clear reset/error flags (GSTAT bits are cleared by writing 1) */
  tmc5160_write(TMC5160_REG_GSTAT, 0x07u);

  /* GCONF = 0: SpreadCycle, no StealthChop - simplest reliable starting
   * point for a bring-up demo. */
  tmc5160_write(TMC5160_REG_GCONF, 0x00000000u);

  /* CHOPCONF: TOFF=3, HSTRT=4, HEND=1, TBL=2 - the datasheet's baseline
   * SpreadCycle configuration. TOFF != 0 is what actually enables the
   * chopper; with TOFF=0 the motor stays unpowered. */
  tmc5160_write(TMC5160_REG_CHOPCONF, 0x000100C3u);

  /* Motor current. GLOBALSCALER scales the full-scale current set by the
   * sense resistors (0 = 256/256 = full scale). 128 = half scale is a
   * gentle default for bring-up -- ADJUST for your R_sense and motor:
   *   I_rms = GLOBALSCALER/256 * (IRUN+1)/32 * V_fs/R_sense * 1/sqrt(2)
   * with V_fs = 0.325 V. */
  tmc5160_write(TMC5160_REG_GLOBALSCALER, 255u);

  /* IHOLD_IRUN: IRUN=16/31 run current, IHOLD=8/31 standstill current,
   * IHOLDDELAY=6 (smooth ramp-down to hold current). */
  tmc5160_write(TMC5160_REG_IHOLD_IRUN, (6u << 16) | (31u << 8) | 8u);

  /* TPOWERDOWN=10: ~0.2 s from standstill to hold-current reduction */
  tmc5160_write(TMC5160_REG_TPOWERDOWN, 10u);

  /* Ramp generator: a modest trapezoid/S-ramp for a first spin.
   * Velocity unit is usteps/t, t = 2^24 / f_clk (~1.4 s at 12 MHz internal
   * clock), so VMAX=200000 is roughly 2.8 rev/s at 256 usteps. */
  tmc5160_write(TMC5160_REG_VSTART, 0u);
  tmc5160_write(TMC5160_REG_A1,     1000u);
  tmc5160_write(TMC5160_REG_V1,     50000u);
  tmc5160_write(TMC5160_REG_AMAX,   5000u);
  tmc5160_write(TMC5160_REG_VMAX,   200000u);
  tmc5160_write(TMC5160_REG_DMAX,   7000u);
  tmc5160_write(TMC5160_REG_D1,     1400u);
  tmc5160_write(TMC5160_REG_VSTOP,  10u);

  /* Positioning mode; make "here" position 0 */
  tmc5160_write(TMC5160_REG_RAMPMODE, TMC5160_RAMPMODE_POSITION);
  tmc5160_write(TMC5160_REG_XACTUAL,  0u);
  tmc5160_write(TMC5160_REG_XTARGET,  0u);

  return true;
}

void tmc5160_move_to(int32_t xtarget)
{
  tmc5160_write(TMC5160_REG_RAMPMODE, TMC5160_RAMPMODE_POSITION);
  tmc5160_write(TMC5160_REG_XTARGET, (uint32_t)xtarget);
}

bool tmc5160_position_reached(void)
{
  return (tmc5160_read(TMC5160_REG_RAMP_STAT) &
          TMC5160_RAMP_STAT_POSITION_REACHED) != 0u;
}
