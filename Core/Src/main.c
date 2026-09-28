#include <stdint.h>

#include "stm32h7xx.h"

void button_init(void);
void uart1_init(void);
void dma1_init(void);
void send_some_data(void);
void uart_polling_send(const char *buf, uint32_t len);
void enable_dma1_stream0(void);
void dma1_interrupt_configuration(void);

// function prototype of callback
void HT_complete_callback(void);
void FT_complete_callback(void);
void TE_complete_callback(void);
void FE_complete_callback(void);
void DME_error_callback(void);

char data_stream[] = "Hello from STM32H750VBT6\r\n";
const uint32_t data_stream_len = sizeof(data_stream);

// Set by the DMA ISR callbacks, polled/consumed from main() or elsewhere.
volatile uint8_t tx_done = 0;
volatile uint8_t tx_error = 0;

#define BASE_ADDR_OF_GPIOA_PERI GPIOA

int main (void)
{
	button_init();
	uart1_init();
	//send_some_data();
	dma1_init();
	dma1_interrupt_configuration();
	// The stream is armed and started from EXTI0_IRQHandler() on each button press,
	// not here - otherwise a transfer fires once automatically at boot.
	while(1);
}

void send_some_data(void)
{
	char somedata[] = "Hello World\r\n";
	uart_polling_send(somedata, sizeof(somedata));
}

void uart_polling_send(const char *buf, uint32_t len)
{
	USART_TypeDef *pUART1;
	pUART1 = USART1;

	for(uint32_t i = 0; i < len; i++)
	{
		// Waiting for TXE to become 1
		while(!(pUART1->ISR & (1 << 7)));

		pUART1->TDR = buf[i];
	}
}

void button_init(void)
{
	// button is connected to PA0
	GPIO_TypeDef *pGPIOA;
	pGPIOA = BASE_ADDR_OF_GPIOA_PERI;
	
	RCC_TypeDef *pRCC;
	pRCC = RCC;
	
	EXTI_TypeDef *pEXTI;
	pEXTI = EXTI;
	
	SYSCFG_TypeDef *pSYSCFG;
	pSYSCFG = SYSCFG;
	
	// 1. enable the peripheral clock for GPIOA peripheral
	pRCC ->AHB4ENR |= (1 << 0);
	
	// 2. Keep the gpio pin in input mode
	pGPIOA->MODER &= ~(0x3 << 0);

	// 2.1: Enable internal pull-up on PA0.
	// The button only connects PA0 to GND (no external pull-up), so without
	// this the pin floats and the falling-edge trigger never sees a clean edge.
	pGPIOA->PUPDR &= ~(0x3 << 0);
	pGPIOA->PUPDR |=  (0x1 << 0);

	// 3. Enable the interrupt over that gpio pin
	pEXTI->IMR1 |= (1 << 0);

	// 4. Enable the clock for SYSCFG
	pRCC->AHB4ENR |= (1 << 1);
	
	// 5. Configuring the SYSCFG CR1 register
	pSYSCFG->EXTICR[0] &= ~(0xF << 0);

	// 6. Configure the edge detection on that gpio pin
	pEXTI->FTSR1 |= (1 << 0);

	// 7. Enable the IRQ related to that gpio pin in NVIC of the processor.
	NVIC_EnableIRQ(EXTI0_IRQn);
}

void uart1_init(void)
{
	RCC_TypeDef *pRCC;
	pRCC = RCC;
	
	GPIO_TypeDef *pGPIOA;
	pGPIOA = BASE_ADDR_OF_GPIOA_PERI;
	
	USART_TypeDef *pUART1;
	pUART1 = USART1;
	
	// 1. enable the peripheral clock for the uart1 peripheral
	pRCC->APB2ENR |= (1 << 4);
	
	// 2. Configure the gpio pins for uart_tx and uart_rx function
		// PA9 as TX, PA10 as RX
	
	// Configure PA9 as UART1 TX
	
	// 2.1: Enable the clock for the GPIOA peripheral
	pRCC ->AHB4ENR |= (1 << 0);
	
	// 2.2: Change the mode of the PA9 to alternate function mode
	pGPIOA->MODER &= ~(0x3 << 18);
	pGPIOA->MODER |=  (0x2 << 18);
	pGPIOA->AFR[1] &= ~(0xF << 4);
	pGPIOA->AFR[1] |=  (0x7 << 4);
	
	// 2.3: Enable or disable Pull-up resistor if required 
	pGPIOA->PUPDR &= ~(0x3 << 18);
	
	// Configure PA10 as UART1 RX

	// 2.4: Change the mode of the PA10 to alternate function mode
	pGPIOA->MODER &= ~(0x3 << 20);
	pGPIOA->MODER |=  (0x2 << 20);
	pGPIOA->AFR[1] &= ~(0xF << 8);
	pGPIOA->AFR[1] |=  (0x7 << 8);
	
	// 2.5: Enable or disable Pull-up resistor if required 
	pGPIOA->PUPDR &= ~(0x3 << 20);

	// 3. Configure the baudrate
	pRCC->D2CFGR  &= ~(0x7 << 8);
	pRCC->D2CFGR  |=  (0x5 << 8);
	
	pUART1->BRR = 0x8B;
	// 4. Configure the data bit width, no of stop bits, etc
	// <No configuration reqd here, we will use default values>

	// 5. Enable the TX engine of the uart peripheral
	pUART1->CR1 |= (1 << 3);

	// 6. Enable the UART peripheral
	pUART1->CR1 |= (1 << 0);

	// 7. Enable DMA mode for transmission (DMAT)
	pUART1->CR3 |= (1 << 7);
}

void dma1_init(void)
{
	RCC_TypeDef *pRCC;
	pRCC = RCC;
	
	DMA_Stream_TypeDef *pSTREAM0;
	pSTREAM0 = DMA1_Stream0;
	
	USART_TypeDef *pUART1;
	pUART1 = USART1;
	
	// 1. Enable peripheral clock for the DMA1
	pRCC->AHB1ENR |= (1 << 0);
	// 2. Identify the stream which is suitable for your peripheral
		// There are no fixed channels/streams like in the STM32F4
		// On the H7, DMA1/DMA2 are connected via the DMAMUX1 block, so ANY stream can be used for USART1_TX:
		// Select stream: for example, DMA1 Stream 0 (or any stream 0–7 of DMA1/DMA2)
		// Corresponding channel: DMA1 Stream 0 → DMAMUX1 Channel 0; DMA1 Stream n → DMAMUX1 Channel n; DMA2 Stream n → DMAMUX1 Channel (8+n)
		// Program DMAMUX: write `DMAMUX1_Channel0->CCR = 41;` 
		// because DMAREQ_ID = 41 is usart1_tx (refer to table 121 "DMAMUX1 request mapping" in Reference Manual RM0433)

	
	// 3. Identify the channel number on which uart1 peripheral send dma request
	// DMAREQ_ID = 42 is USART1_TX (41 is USART1_RX - do not confuse the two)
	DMAMUX1_Channel0->CCR = 42;

	// 4. Program the source address(memory)
	pSTREAM0->M0AR = (uint32_t)data_stream;
	
	// 5. Program the destination address(peripheral)(TDR)
	pSTREAM0->PAR = (uint32_t) &pUART1->TDR;

	// 6. Program number of data item to send
	pSTREAM0->NDTR = data_stream_len;
	
	// 7. The direction of data transfer: m2p, p2m, m2m -> choose m2p
	// DIR[1:0] is bits 7:6 of SxCR. 00=P2M, 01=M2P, 10=M2M.
	pSTREAM0->CR &= ~(0x3 << 6);
	pSTREAM0->CR |=  (0x1 << 6);
	// Make sure PFCTRL (bit 5) stays 0: DMA must be the flow controller for USART,
	// since USART doesn't support peripheral-flow-controller mode.
	pSTREAM0->CR &= ~(1 << 5);
	
	// 8. Program the source and destination data width
	pSTREAM0->CR &= ~(0x3 << 13);	// MSIZE
	pSTREAM0->CR &= ~(0x3 << 11);   // PSIZE
		// Enable MINC (Memory Increment)
	pSTREAM0->CR |= (1 << 10);
	// 9. Direct mode or fifo mode
		// Keep Direct mode enabled (FCR default), simplest for byte-sized transfers.
		// (Enabling FIFO mode with FTH=FULL while PSIZE=byte/single-burst is an
		// invalid/reserved combination per RM0433 and stalls the stream with FEIF.)

	// 10. Select the fifo threshold
		// N/A - direct mode in use

	// 11. Enable the circular mode if required

	// 12. Single transfer or burst transfer

	// 13. Configure the stream priority

	// 14. Do NOT enable the stream here.
	// The stream is armed and started from EXTI0_IRQHandler() on each button press.
}

void enable_dma1_stream0(void)
{
	DMA_Stream_TypeDef *pSTREAM0;
	pSTREAM0 = DMA1_Stream0;
	
	// Enable the stream
	pSTREAM0->CR |= (1 << 0);
}

void dma1_interrupt_configuration(void)
{

	DMA_Stream_TypeDef *pSTREAM0;
	pSTREAM0 = DMA1_Stream0;
	
	//1. Lets do Half-transfer IE(HTIE)
	pSTREAM0->CR |= (1 << 3);
	
	//2. Transfer complete IE(TCIE)
	pSTREAM0->CR |= (1 << 4);
	
	//3. Transfer error IE(TEIE)
	pSTREAM0->CR |= (1 << 2);
	
	//4. FIFO overrun/underrun IE(FEIE)
	pSTREAM0->FCR |= (1 << 7);
	
	//5. Direct mode error(DMEIE)
	pSTREAM0->CR |= (1 << 1);
	
	//6. Enable the IRQ for DMA1 stream0 global interrupt in NVIC
	NVIC_EnableIRQ(DMA1_Stream0_IRQn);
	
}

void HT_complete_callback(void)
{
	// Only meaningful for large/circular buffers where the first half can be
	// processed while DMA fills the second half. Not used for this single
	// short message transfer.
}

void FT_complete_callback(void)
{
	// Transfer complete: the full message has been shifted out to USART1->TDR.
	tx_done = 1;
}

void TE_complete_callback(void)
{
	// Transfer error (e.g. bad PAR/M0AR address). This is a real fault.
	tx_error = 1;
}

void FE_complete_callback(void)
{
	// FIFO error. In Direct mode this flag can be set as a benign
	// underrun/overrun indicator even on a successful transfer, so it is not
	// treated as a hard error here - just observe it if needed for debugging.
}

void DME_error_callback(void)
{
	// Direct-mode error (e.g. mismatched MSIZE/PSIZE). This is a real fault.
	tx_error = 1;
}
