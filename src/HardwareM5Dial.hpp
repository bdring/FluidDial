constexpr static const int ENC_A           = GPIO_NUM_40;
constexpr static const int ENC_B           = GPIO_NUM_41;
constexpr static const int DIAL_BUTTON_PIN = GPIO_NUM_42;

#include "M5Dial.h"

constexpr static const int FNC_UART_NUM = 1;
#ifdef I2C_BUTTONS
// PORT B is used for I2C buttons
constexpr static const int I2C_BUTTONS_ADDR = 0x20;
constexpr static const int I2C_BUTTONS_SDA  = GPIO_NUM_2;  // SDA
constexpr static const int I2C_BUTTONS_SCL  = GPIO_NUM_1;  // SCL
// PORT A is used for UART
constexpr static const int PND_RX_FNC_TX_PIN = GPIO_NUM_15;
constexpr static const int PND_TX_FNC_RX_PIN = GPIO_NUM_13;
#else
#ifdef UART_ON_PORT_B
constexpr static const int RED_BUTTON_PIN    = GPIO_NUM_13;
constexpr static const int GREEN_BUTTON_PIN  = GPIO_NUM_15;
constexpr static const int PND_RX_FNC_TX_PIN = GPIO_NUM_1;
constexpr static const int PND_TX_FNC_RX_PIN = GPIO_NUM_2;

#else  // UART is on PORT A
// This pin assignment avoids a problem whereby touch will not work
// if the pendant is powered independently of the FluidNC controller
// and the pendant is power-cycled while the FluidNC controller is on.
// The problem is caused by back-powering of the 3V3 rail through the
// M5Stamp's Rx pin.  When RX is on GPIO1, 3V3 from the FluidNC Tx line
// causes the M5Stamp 3V3 rail to float at 1.35V, which in turn prevents
// the touch chip from starting properly after full power is applied.
// The touch function then does not work.
// When RX is on GPIO15, the back-powering drives the 3V3 rail only to
// 0.3V and everything starts correctly.
constexpr static const int RED_BUTTON_PIN    = GPIO_NUM_1;
constexpr static const int GREEN_BUTTON_PIN  = GPIO_NUM_2;
constexpr static const int PND_RX_FNC_TX_PIN = GPIO_NUM_15;
constexpr static const int PND_TX_FNC_RX_PIN = GPIO_NUM_13;
#endif
#define WAKEUP_GPIO RED_BUTTON_PIN
#endif  // I2C_BUTTONS