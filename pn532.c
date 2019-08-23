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

void *
pn532_end (pn532_t * p)
{                               // Close
   if (p)
   {
      uart_driver_delete (p->uart);
      free (p);
   }
   return NULL;
}

pn532_t *
pn532_init (int uart, int tx, int rx, uint8_t p3)
{                               // Init PN532
   pn532_t *p = malloc (sizeof (*p));
   if (!p)
      return p;
   p->uart = uart;
   p->tx = tx;
   p->rx = rx;
   // Init UART
   uart_config_t uart_config = {
      .baud_rate = 115200,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
   };
   esp_err_t err;
   if ((err = uart_param_config (uart, &uart_config)) || (err = uart_set_pin (uart, tx, rx, -1, -1))
       || (err = uart_driver_install (uart, 1024, 1024, 0, NULL, 0)))
   {
      ESP_LOGE (TAG, "UART fail %s", esp_err_to_name (err));
      free (p);
      return NULL;
   }
   uint8_t buf[8];
   // Poke serial
   buf[0] = 0x55;
   buf[1] = 0x55;
   buf[2] = 0x00;
   buf[3] = 0x00;
   buf[4] = 0x00;
   uart_write_bytes (uart, (char *) buf, 5);
   // Set up PN532
   buf[0] = 0x02;               // GetFirmwareVersion
   if (pn532_send (p, 1, buf) < 0 || pn532_recv (p, sizeof (buf), buf) < 4)
      return pn532_end (p);
   //uint32_t ver = (buf[0] << 24) + (buf[1] << 16) + (buf[2] << 8) + buf[3];
   buf[0] = 0x32;               // RFConfiguration
   buf[1] = 5;                  // Config item 5 (MaxRetries)
   buf[2] = 0xFF;               // MxRtyATR (default = 0xFF)
   buf[3] = 0x01;               // MxRtyPSL (default = 0x01)
   buf[4] = 0x01;               // MxRtyPassiveActivation
   if (pn532_send (p, 5, buf) < 0 || pn532_recv (p, sizeof (buf), buf) < 0)
      return pn532_end (p);
   buf[0] = 0x14;               // SAMConfiguration
   buf[1] = 0x01;               // Normal
   buf[2] = 20;                 // *50ms timeout
   buf[3] = 0x01;               // Use IRQ
   if (pn532_send (p, 4, buf) < 0 || pn532_recv (p, sizeof (buf), buf) < 0)
      return pn532_end (p);
   buf[0] = 0x08;               // WriteRegister
   buf[1] = 0xFF;               // P3CFGB
   buf[2] = 0xFD;               // P3CFGB
   buf[3] = p3;                 // Define output bits
   buf[4] = 0xFF;               // P3
   buf[5] = 0xB0;               // P3
   buf[6] = 0xFF;               // All high
   if (pn532_send (p, 7, buf) < 0 || pn532_recv (p, sizeof (buf), buf) < 0)
      return pn532_end (p);
   buf[0] = 0x32;               // RFConfiguration
   buf[1] = 0x04;               // MaxRtyCOM
   buf[2] = 1;                  // Retries (default 0)
   if (pn532_send (p, 3, buf) < 0 || pn532_recv (p, sizeof (buf), buf) < 0)
      return pn532_end (p);
   buf[0] = 0x32;               // RFConfiguration
   buf[1] = 0x02;               // Various timings (100*2^(n-1))us
   buf[2] = 0x00;               // RFU
   buf[3] = 0x0B;               // Default (102.4 ms)
   buf[4] = 0x0A;               // Default is 0x0A (51.2 ms)
   if (pn532_send (p, 5, buf) || pn532_recv (p, sizeof (buf), buf) < 0)
      return pn532_end (p);
   return p;
}

// Low level access functions
int
pn532_send (pn532_t * p, int len, uint8_t * data)
{                               // Send data to PN532
   // TODO
   // TODO hex debug log
   return -1;
}

int
pn532_recv (pn532_t * p, int max, uint8_t * data)
{                               // Recv data from PN532
   // TODO
   // TODO hex debug log
   return -1;
}

// Data exchange (for DESFire use)
int
pn532_dx (pn532_t * p, unsigned int len, uint8_t * data, unsigned int max)
{                               // Card access function - sends to card starting CMD byte, and receives reply in to same buffer, starting status byte, returns len
   // TODO
   return -1;
}

// Other higher level functions
