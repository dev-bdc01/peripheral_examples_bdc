/***************************************************************************//**
 * @file main.c
 * @brief This project demonstrates DMA driven pulse width modulation using the
 * TIMER module. The GPIO pin specified in the readme.txt is configured to
 * output a 1kHz signal. The DMA continuously updates the CCVB register to vary
 * the duty cycle.
 *******************************************************************************
 * # License
 * <b>Copyright 2020 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * SPDX-License-Identifier: Zlib
 *
 * The licensor of this software is Silicon Laboratories Inc.
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 *
 *******************************************************************************
 * # Evaluation Quality
 * This code has been minimally tested to ensure that it builds and is suitable 
 * as a demonstration for evaluation purposes only. This code will be maintained
 * at the sole discretion of Silicon Labs.
 ******************************************************************************/

#include "em_device.h"
#include "em_cmu.h"
#include "em_emu.h"
#include "em_chip.h"
#include "em_gpio.h"
#include "em_timer.h"
#include "em_ldma.h"

// Note: change this to set the desired Output frequency in Hz
#define PWM_FREQ 1000

// Buffer size
#define BUFFER_SIZE 11

// Note: change this to change the duty cycles used in this example
static const uint16_t dutyCyclePercentages[BUFFER_SIZE] =
    {0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100};

// Buffer of duty cycle values for DMA transfer to CCVB
// Buffer is populated after TIMER is initialized and Top value is set
static uint16_t buffer[BUFFER_SIZE];

/**************************************************************************//**
 * @brief
 *    GPIO initialization
 *****************************************************************************/
void initGpio(void)
{
  // Configure PA6 as output
  GPIO_PinModeSet(gpioPortA, 6, gpioModePushPull, 0);
}

/**************************************************************************//**
 * @brief
 *    CMU initialization
 *****************************************************************************/
void initCmu(void)
{
  // Enable clock to GPIO and TIMER0
  CMU_ClockEnable(cmuClock_GPIO, true);
  CMU_ClockEnable(cmuClock_TIMER0, true);
  /* Note: For EFR32xG21 radio devices, library function calls to
   * CMU_ClockEnable() have no effect as oscillators are automatically turned
   * on/off based on demand from the peripherals; CMU_ClockEnable() is a dummy
   * function for EFR32xG21 for library consistency/compatibility.
   */
}

/**************************************************************************//**
 * @brief
 *    TIMER initialization
 *****************************************************************************/
void initTimer(void)
{
  // Initialize and start timer with no prescaling
  TIMER_Init_TypeDef timerInit = TIMER_INIT_DEFAULT;
  // Configure TIMER0 Compare/Capture for output compare
  TIMER_InitCC_TypeDef timerCCInit = TIMER_INITCC_DEFAULT;

  // Use PWM mode, which sets output on overflow and clears on compare events
  timerInit.enable = false;
  timerCCInit.mode = timerCCModePWM;

  // Configure but do not start the timer
  TIMER_Init(TIMER0, &timerInit);

  // Route Timer0 CC0 output to PA6
  GPIO->TIMERROUTE[0].ROUTEEN  = GPIO_TIMER_ROUTEEN_CC0PEN;
  GPIO->TIMERROUTE[0].CC0ROUTE = (gpioPortA << _GPIO_TIMER_CC0ROUTE_PORT_SHIFT)
                    | (6 << _GPIO_TIMER_CC0ROUTE_PIN_SHIFT);

  TIMER_InitCC(TIMER0, 0, &timerCCInit);

  // Set top value to overflow at the desired PWM_FREQ frequency
  TIMER_TopSet(TIMER0, CMU_ClockFreqGet(cmuClock_TIMER0) / PWM_FREQ);

  // Start the timer
  TIMER_Enable(TIMER0, true);

  // Trigger DMA on compare event to set CCVB to update duty cycle on next period
  TIMER_IntEnable(TIMER0, TIMER_IEN_CC0);
}

/**************************************************************************//**
 * @brief
 *    Populate buffer with timer duty cycle values
 *****************************************************************************/
void populateBuffer(void)
{
  for (uint32_t i = 0; i < BUFFER_SIZE; i++) {
    buffer[i] = (uint16_t) (TIMER_TopGet(TIMER0) * dutyCyclePercentages[i] / 100);
  }
}

/**************************************************************************//**
* @brief
*    Initialize the LDMA module
*
* @details
*    Configure the channel descriptor to use the default peripheral to
*    memory transfer descriptor. Modify it to not generate an interrupt
*    upon transfer completion (we don't need it). Additionally, the transfer
*    configuration selects the TIMER0_CC0 signal as the trigger for the LDMA
*    transaction to occur.
*
* @note
*    The descriptor object needs to at least have static scope persistence so
*    that the reference to the object is valid beyond its first use in
*    initialization
*****************************************************************************/
void initLdma(void)
{
  // Start the transfer
  uint32_t channelNum = 0;

  // LDMA initialization
  LDMA_Init_t init = LDMA_INIT_DEFAULT;
  LDMA_Init(&init);

  // Transfer configuration and trigger selection
  LDMA_TransferCfg_t transferConfig =
    LDMA_TRANSFER_CFG_PERIPHERAL(ldmaPeripheralSignal_TIMER0_CC0);

  // Channel descriptor configuration
  static LDMA_Descriptor_t descriptor =
    LDMA_DESCRIPTOR_LINKREL_M2P_BYTE(&buffer,            // Memory source address
                                    &TIMER0->CC[0].OCB, // Peripheral destination address
                                    BUFFER_SIZE,         // Number of bytes per transfer
                                    0);                  // Link to same descriptor
  descriptor.xfer.size     = ldmaCtrlSizeHalf; // Unit transfer size
  descriptor.xfer.doneIfs  = 0;                // Don't trigger interrupt when done

  LDMA_StartTransfer(channelNum, &transferConfig, &descriptor);
}

/**************************************************************************//**
 * @brief  Main function
 *****************************************************************************/
int main(void)
{
  // Chip errata
  CHIP_Init();

  // Initializations
  initCmu();
  initGpio();
  initTimer();

  // Initialize DMA only after buffer is populated
  populateBuffer();
  initLdma();

  while (1) {
    EMU_EnterEM1(); // Enter EM1 (won't exit)
  }
}

