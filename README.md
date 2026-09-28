# M2P_UART_FROM_SCRATCH

Bare-metal UART + DMA driver for the **STM32H750VBTx** (ARM Cortex-M7) written entirely from scratch — no HAL libraries, just direct register-level programming.

> **Created:** September 22, 2026

## 📋 Overview

This project demonstrates how to configure and use **USART1** together with **DMA1 (Memory-to-Peripheral)** on the STM32H7 without relying on the STM32 HAL abstraction layer. All peripheral initialization is done by directly manipulating MCU registers, making it an excellent learning resource for understanding the low-level workings of the STM32H7 platform.

### Key Features

- **USART1 Configuration** — TX (PA9) and RX (PA10) set up in alternate-function mode with direct register access.
- **Bare-metal Polling Transmission** — `uart_polling_send()` sends bytes over UART by waiting on the TXE flag; used by `send_some_data()`.
- **GPIO Button with EXTI Interrupt** — A user button on **PA0** triggers an external interrupt (`EXTI0`) on the falling edge, with internal pull-up enabled (button wired directly to GND) and a software debounce guard.
- **DMA1 M2P UART Transmission** — DMA1 Stream0, routed through DMAMUX1 (request `USART1_TX`), transmits `data_stream[]` to `USART1->TDR` in Memory-to-Peripheral direct mode.
- **Button-triggered DMA Transfer** — Each button press (debounced) re-arms and starts DMA1 Stream0 from `EXTI0_IRQHandler`, sending the full message over UART1.
- **DMA1 Stream0 Interrupts** — Half-transfer, transfer-complete, transfer-error, FIFO-error and direct-mode-error interrupts are all enabled, each dispatched from `DMA1_Stream0_IRQHandler` to its own callback.
- **NVIC Configuration** — Enables and handles both the `EXTI0_IRQn` and `DMA1_Stream0_IRQn` interrupts in the Nested Vectored Interrupt Controller.

## 🛠 Hardware

| Component        | Details                              |
|------------------|---------------------------------------|
| MCU              | STM32H750VBTx (Cortex-M7)            |
| Package          | LQFP100                              |
| UART             | USART1                               |
| UART TX Pin      | PA9 (AF7)                            |
| UART RX Pin      | PA10 (AF7)                           |
| Button Pin       | PA0 (EXTI0, falling edge, internal pull-up), wired directly to GND |
| DMA              | DMA1 Stream0, DMAMUX1 request ID 42 (USART1_TX), IRQ-driven (`DMA1_Stream0_IRQn`) |
| Toolchain        | Keil MDK-ARM                         |

## 📁 Project Structure

```
M2P_UART_FROM_SCRATCH/
├── Core/
│   ├── Inc/
│   │   ├── main.h
│   │   ├── stm32h7xx_hal_conf.h
│   │   └── stm32h7xx_it.h
│   └── Src/
│       ├── main.c                  # Main application & peripheral init
│       ├── stm32h7xx_it.c          # Interrupt handlers (EXTI0)
│       └── system_stm32h7xx.c      # System clock configuration
├── Drivers/
│   └── CMSIS/                      # CMSIS device headers
├── MDK-ARM/                        # Keil MDK-ARM project files
│   ├── M2P_UART_FROM_SCRATCH.uvprojx
│   └── startup_stm32h750xx.s
├── prompt/
│   └── Readme.md                   # This file
└── M2P_UART_FROM_SCRATCH.ioc       # STM32CubeMX project file
```

## 🔧 How It Works

### 1. Button Initialization (`button_init`)

- Enables the AHB4 clock for **GPIOA**.
- Configures **PA0** as input mode with **internal pull-up** enabled (the button only connects PA0 to GND, no external pull-up on the board).
- Enables the **EXTI0** interrupt line with falling-edge detection.
- Configures **SYSCFG** to route PA0 to EXTI0.
- Enables the `EXTI0_IRQn` interrupt in the NVIC.

### 2. UART Initialization (`uart1_init`)

- Enables the APB2 clock for **USART1**.
- Configures **PA9** (TX) and **PA10** (RX) as alternate-function mode (AF7).
- Sets the baud rate via the `BRR` register (`0x8B`).
- Enables the UART transmitter and the UART peripheral.
- Enables **DMAT** (`CR3` bit 7) so USART1 can raise DMA requests on TXE.

### 3. Polling Data Transmission (`send_some_data` / `uart_polling_send`)

- Polls the **TXE** (Transmit Data Register Empty) flag in the USART `ISR`.
- Writes each byte of a buffer to the `TDR` register. Used for simple, blocking test transfers.

### 4. DMA Initialization (`dma1_init`)

- Enables the AHB1 clock for **DMA1**.
- Routes **DMAMUX1 Channel0** to **DMAREQ_ID 42 (USART1_TX)** — not to be confused with ID 41, which is USART1_RX.
- Programs `M0AR` (source, `data_stream[]`) and `PAR` (destination, `USART1->TDR`).
- Programs `NDTR` with the message length.
- Sets direction to **Memory-to-Peripheral** (`DIR[1:0]` = `01`, bits 7:6 of `SxCR`) and keeps `PFCTRL` (bit 5) at 0, since DMA — not the peripheral — must be the flow controller.
- Enables **MINC** (memory increment) so each transferred byte advances the source pointer.
- Uses Direct mode (FIFO left disabled) since MSIZE/PSIZE are both byte-sized.
- Does **not** enable the stream here — the stream is armed and started only from `EXTI0_IRQHandler`.

### 5. DMA Interrupt Configuration (`dma1_interrupt_configuration`)

- Enables Half-Transfer (`HTIE`), Transfer-Complete (`TCIE`), Transfer-Error (`TEIE`) and Direct-Mode-Error (`DMEIE`) interrupts in `DMA1_Stream0->CR`, and FIFO-Error (`FEIE`) in `DMA1_Stream0->FCR`.
- Enables `DMA1_Stream0_IRQn` in the NVIC.
- Called once from `main()`, after `dma1_init()` and before entering the idle loop. The stream itself is still armed and started only from `EXTI0_IRQHandler` on each button press — enabling these interrupts does not start a transfer.

### 6. Button Interrupt Handling (`EXTI0_IRQHandler`)

On each button press:
1. Masks the EXTI0 line (debounce guard) so switch bounce can't re-enter the handler mid-transfer.
2. Disables DMA1 Stream0 and waits for `EN` to clear before reconfiguring it.
3. Clears all DMA1 Stream0 status flags (`LIFCR`).
4. Reloads `NDTR` with the message length.
5. Re-enables the stream, starting a fresh DMA transfer of `data_stream[]` to `USART1->TDR`.
6. Busy-waits (an uncalibrated cycle-count delay, since the project does not configure the clock tree or use SysTick) to let the mechanical switch settle, clears any pending flag raised by bounce, then unmasks EXTI0. If a single press still produces more than one transfer, this window is too short for your switch/wiring and the iteration count should be increased, or a hardware debounce (e.g. a capacitor across the button) added.

### 7. DMA Interrupt Handling (`DMA1_Stream0_IRQHandler`)

Checks the DMA1 low interrupt status register (`LISR`) for stream0's flags and, for whichever one is set, clears it in `LIFCR` and calls the matching callback defined in `main.c`:

| Flag  | Callback                | Meaning |
|-------|--------------------------|---------|
| HTIF0 | `HT_complete_callback`   | Half of the buffer transferred. Only meaningful for large/circular buffers; unused for this single short message. |
| TCIF0 | `FT_complete_callback`   | Transfer complete — the full message reached `USART1->TDR`. Sets `tx_done = 1`. |
| TEIF0 | `TE_complete_callback`   | Transfer error (e.g. invalid address). A real fault — sets `tx_error = 1`. |
| FEIF0 | `FE_complete_callback`   | FIFO error. In Direct mode this can be set as a benign side effect even on a successful transfer, so it is not treated as a hard error. |
| DMEIF0| `DME_error_callback`    | Direct-mode error (e.g. mismatched MSIZE/PSIZE). A real fault — sets `tx_error = 1`. |

`tx_done` and `tx_error` (`volatile uint8_t`, defined in `main.c`) are simple flags any other code (e.g. the main loop) can poll to know the outcome of the last DMA transfer.

## 🚀 Building & Flashing

1. Open **Keil µVision**.
2. Open the project file: `MDK-ARM/M2P_UART_FROM_SCRATCH.uvprojx`.
3. Build the project (`F7`).
4. Flash to the target board (`F8`).
5. Open a serial terminal on USART1's port and press the button on PA0 — `"Hello from STM32H750VBT6\r\n"` is sent via DMA on each press.

## 📖 Notes

- This project is intentionally written **without the HAL library** to illustrate bare-metal register-level programming on the STM32H7.
- The STM32CubeMX `.ioc` file is included for reference but the generated HAL code is not used at runtime.
- Common pitfalls hit and fixed during development: duplicate global definitions across translation units, `sizeof()` on an incomplete `extern` array type, an invalid FIFO threshold/burst combination, mixing up the `DIR` and `PFCTRL` bit positions in `DMA_SxCR`, using the wrong DMAMUX request ID (USART1_RX instead of USART1_TX), accidentally starting the DMA stream once automatically at boot instead of only from the button ISR, and an under-calibrated software debounce window causing a single press to sometimes produce two transfers.

## 📜 License

This project is provided for educational purposes. Feel free to use and modify it for learning.
