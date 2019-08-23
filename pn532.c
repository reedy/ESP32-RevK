// PN532 functions
static const char TAG[] = "PN532";

#include "pn532.h"
#include "esp_log.h"
#include <driver/uart.h>

struct pn532_s
{
   uint8_t uart;                // Which UART
   uint8_t tx;                  // Tx GPIO
   uint8_t rx;                  // Rx GPIO
};


pn532_t *
pn532_init (int uart, int tx, int rx)
{                               // Init PN532
   pn532_t *p = malloc (sizeof (*p));
   if (!p)
      return p;
   p->uart = uart;
   p->tx = tx;
   p->rx = rx;
   // Init UART
   const int uart_num = UART_NUM_2;
   uart_config_t uart_config = {
      .baud_rate = 115200,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
   };
   esp_err_t err;
   if ((err = uart_param_config (uart_num, &uart_config)))
   {
      ESP_LOGE (TAG, "UART fail %s", esp_err_to_name (err));
      free (p);
      return NULL;
   }
   // Set up PN532

   return p;
}

// Low level access functions
int
pn532_send (pn532_t * p, int len, uint8_t * data)
{                               // Send data to PN532
   // TODO
   return -1;
}

int
pn532_recv (pn532_t * p, int max, uint8_t * data)
{                               // Recv data from PN532
   // TODO
   return -1;
}


int
pn532_dx (pn532_t * p, unsigned int len, unsigned char *data, unsigned int max)
{                               // Card access function - sends to card starting CMD byte, and receives reply in to same buffer, starting status byte, returns len
   // TODO
   return -1;
}
