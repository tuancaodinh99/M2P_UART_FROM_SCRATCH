/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "stm32h7xx_it.h"
#include "stm32h750xx.h"

extern const uint32_t data_stream_len;

extern void HT_complete_callback(void);
extern void FT_complete_callback(void);
extern void TE_complete_callback(void);
extern void FE_complete_callback(void);
extern void DME_error_callback(void);

void clear_extti_pending_bit (void)
{
// Clears the Pending bit
	if(EXTI->PR1 & (1 << 0))
	{
		EXTI->PR1 = (1U << 0);  // write 1 to clear pending flag
	}
}

/* --------------------------------IRQ handlers ------------------------------*/

// IRQ handler for button interrupt
void EXTI0_IRQHandler(void)
{
	DMA_Stream_TypeDef *pSTREAM0;
	pSTREAM0 = DMA1_Stream0;

	// 0. Mask the EXTI0 line so mechanical switch bounce (extra edges arriving
	// within the next few ms) cannot re-enter this handler and restart the
	// DMA transfer mid-way through.
	EXTI->IMR1 &= ~(1 << 0);

	// 1. Disable the stream before reconfiguring it (EN must be 0 to write NDTR)
	pSTREAM0->CR &= ~(1 << 0);
	while (pSTREAM0->CR & (1 << 0));

	// 2. Clear all pending flags for DMA1 Stream0 (FEIF/DMEIF/TEIF/HTIF/TCIF, bits 0-5)
	DMA1->LIFCR |= (0x3F << 0);

	// 3. Reload the number of bytes to transfer
	pSTREAM0->NDTR = data_stream_len;

	// 4. Re-enable the stream to start a new transfer
	pSTREAM0->CR |= (1 << 0);

	// 5. Debounce delay: let the switch settle before accepting the next press.
	// This is an uncalibrated busy-wait (no clock config / SysTick in this
	// project yet), so the actual delay depends on the core clock. If you
	// still see a press produce two full messages, this window is too short
	// for your switch/wiring - increase the iteration count further.
	for (volatile uint32_t d = 0; d < 3000000; d++);

	// 6. Discard any pending flag raised by bounce while masked, then unmask.
	clear_extti_pending_bit();
	EXTI->IMR1 |= (1 << 0);
}

// IRQ handler for DMA1 stream0 global interrupt
#define is_it_HT()  DMA1->LISR & (1 << 4)
#define is_it_FT()  DMA1->LISR & (1 << 5)
#define is_it_TE()  DMA1->LISR & (1 << 3)
#define is_it_FE()  DMA1->LISR & (1 << 0)
#define is_it_DME() DMA1->LISR & (1 << 2)

void DMA1_Stream0_IRQHandler(void)
{
	// Half-transfer
	if(is_it_HT())
	{
		DMA1->LIFCR |= (1 << 4);
		HT_complete_callback();
	}
	else if(is_it_FT())
	{
		DMA1->LIFCR |= (1 << 5);
		FT_complete_callback();
	}
	else if(is_it_TE())
	{
		DMA1->LIFCR |= (1 << 3);
		TE_complete_callback();
	}
	else if(is_it_FE())
	{
		DMA1->LIFCR |= (1 << 0);
		FE_complete_callback();
	}
	else if(is_it_DME())
	{
		DMA1->LIFCR |= (1 << 2);
		DME_error_callback();
	}
	else
	{
		
	}

}
