// PN532 functions
static const char TAG[] = "PN532";

#include "pn532.h"
#include "esp_log.h"
#include <driver/uart.h>

#define	HEXLOG ESP_LOG_INFO

struct pn532_s
{
   uint8_t uart;                // Which UART
   uint8_t tx;                  // Tx GPIO
   uint8_t rx;                  // Rx GPIO
   uint8_t pending;             // Pending response
   uint8_t cards;               // Cards present (0, 1 or 2)
   uint8_t cardidlen;           // Length of last card ID read
   uint8_t cardid[10];          // Binary card id of last card ID read
};

static int
uart_rx (pn532_t * p, uint8_t * buf, uint32_t length, int ms)
{                               // Low level UART rx with optional logging
   ms /= portTICK_PERIOD_MS;
   if (ms < 1)
      ms = 1;
   int l = uart_read_bytes (p->uart, buf, length, ms);
#ifdef HEXLOG
   if (l > 0)
      ESP_LOG_BUFFER_HEX_LEVEL ("NFCRx", buf, l, HEXLOG);
   if (l != length)
      ESP_LOGI (TAG, "Rx %d/%d %d*%dms", l, length, ms, portTICK_PERIOD_MS);
#endif
   return l;
}

static int
uart_tx (pn532_t * p, const uint8_t * src, size_t size)
{                               // Low level UART tx with optional logging
   int l = uart_write_bytes (p->uart, (char *) src, size);
#ifdef HEXLOG
   if (l > 0)
      ESP_LOG_BUFFER_HEX_LEVEL ("NFCTx", src, l, HEXLOG);
   if (l != size)
      ESP_LOGI (TAG, "Tx %d/%d", l, size);
#endif
   uart_wait_tx_done (p->uart, 100 / portTICK_PERIOD_MS);
   return l;
}

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
   memset (p, 0, sizeof (*p));
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
       || (err = uart_driver_install (uart, 280, 0, 0, NULL, 0)) || (err = uart_set_mode (uart, UART_MODE_UART)))
   {
      ESP_LOGE (TAG, "UART fail %s", esp_err_to_name (err));
      free (p);
      return NULL;
   }
   ESP_LOGD (TAG, "PN532 UART %d Tx %d Rx %d", uart, tx, rx);
   uint8_t buf[8];
   // Set up PN532 (SAM first as in vLowBat more)
   pn532_kick (p);
   // SAMConfiguration
   buf[0] = 0x01;               // Normal
   buf[1] = 20;                 // *50ms timeout
   buf[2] = 0x01;               // Use IRQ
   if (pn532_tx (p, 0x14, 0, NULL, 3, buf) < 0 || pn532_rx (p, 0, NULL, sizeof (buf), buf) < 0)
      return pn532_end (p);
   // GetFirmwareVersion
   if (pn532_tx (p, 0x02, 0, NULL, 0, NULL) < 0 || pn532_rx (p, 0, NULL, sizeof (buf), buf) < 0)
      return pn532_end (p);
   //uint32_t ver = (buf[0] << 24) + (buf[1] << 16) + (buf[2] << 8) + buf[3];
   // RFConfiguration
   buf[0] = 5;                  // Config item 5 (MaxRetries)
   buf[1] = 0xFF;               // MxRtyATR (default = 0xFF)
   buf[2] = 0x01;               // MxRtyPSL (default = 0x01)
   buf[3] = 0x01;               // MxRtyPassiveActivation
   if (pn532_tx (p, 0x32, 0, NULL, 4, buf) < 0 || pn532_rx (p, 0, NULL, sizeof (buf), buf) < 0)
      return pn532_end (p);
   // WriteRegister
   buf[0] = 0xFF;               // P3CFGB
   buf[1] = 0xFD;               // P3CFGB
   buf[2] = p3;                 // Define output bits
   buf[3] = 0xFF;               // P3
   buf[4] = 0xB0;               // P3
   buf[5] = 0xFF;               // All high
   if (pn532_tx (p, 0x08, 0, NULL, 6, buf) < 0 || pn532_rx (p, 0, NULL, sizeof (buf), buf) < 0)
      return pn532_end (p);
   // RFConfiguration
   buf[0] = 0x04;               // MaxRtyCOM
   buf[1] = 1;                  // Retries (default 0)
   if (pn532_tx (p, 0x32, 0, NULL, 2, buf) < 0 || pn532_rx (p, 0, NULL, sizeof (buf), buf) < 0)
      return pn532_end (p);
   // RFConfiguration
   buf[0] = 0x02;               // Various timings (100*2^(n-1))us
   buf[1] = 0x00;               // RFU
   buf[2] = 0x0B;               // Default (102.4 ms)
   buf[3] = 0x0A;               // Default is 0x0A (51.2 ms)
   if (pn532_tx (p, 0x32, 0, NULL, 4, buf) || pn532_rx (p, 0, NULL, sizeof (buf), buf) < 0)
      return pn532_end (p);
   return p;
}

// Low level access functions
int
pn532_tx (pn532_t * p, uint8_t cmd, int len1, uint8_t * data1, int len2, uint8_t * data2)
{                               // Send data to PN532
   if (p->pending)
      return -1;                // Command outstanding
   uint8_t buf[10],
    *b = buf;
   *b++ = 0x00;                 // Preamble
   *b++ = 0x00;                 // Start 1
   *b++ = 0xFF;                 // Start 2
   int l = len1 + len2 + 2;
   if (l >= 0x100)
   {
      *b++ = 0xFF;              // Extended len
      *b++ = 0xFF;
      *b++ = (l >> 8);          // len
      *b++ = (l & 0xFF);
      *b++ = -(l >> 8) - (l & 0xFF);    // Checksum
   } else
   {
      *b++ = l;                 // Len
      *b++ = -l;                // Checksum
   }
   *b++ = 0xD4;                 // Direction (host to PN532)
   *b++ = cmd;
   uint8_t sum = 0xD4 + cmd;
   for (l = 0; l < len1; l++)
      sum += data1[l];
   for (l = 0; l < len2; l++)
      sum += data2[l];
   // Send data
   uart_flush_input (p->uart);
   uart_tx (p, buf, b - buf);
   if (len1)
      uart_tx (p, data1, len1);
   if (len2)
      uart_tx (p, data2, len2);
   buf[0] = -sum;               // Checksum
   buf[1] = 0x00;               // Postamble
   uart_tx (p, buf, 2);
   // Get ACK and check it
   l = uart_rx (p, buf, 6, 10);
   if (l < 6 || buf[0] || buf[1] || buf[2] != 0xFF || buf[3] || buf[4] != 0xFF || buf[5])
      return -1;                // Bad
   p->pending = cmd + 1;
   return 0;                    // OK
}

int
pn532_rx (pn532_t * p, int max1, uint8_t * data1, int max2, uint8_t * data2)
{                               // Recv data from PN532
   if (!p->pending)
      return -14;               // No response pending
   uint8_t pending = p->pending;
   p->pending = 0;
   uint8_t buf[9];
   int l = uart_rx (p, buf, 7, 100);
   if (l < 7 || buf[0] || buf[1] || buf[2] != 0xFF)
      return -1;
   int len = 0;
   if (buf[3] == 0xFF && buf[4] == 0xFF)
   {                            // Extended
      l = uart_rx (p, buf + 7, 3, 10);
      if (l != 6)
         return -1;
      if ((uint8_t) (buf[5] + buf[6] + buf[7]))
         return -2;             // Bad checksum
      len = (buf[5] << 8) + buf[6];
      if (buf[8] != 0xD5)
         return -3;             // Not reply
      if (buf[9] != pending)
         return -4;
   } else
   {                            // Normal
      if ((uint8_t) (buf[3] + buf[4]))
         return -4;             // Bad checksum
      len = buf[3];
      if (buf[5] != 0xD5)
         return -5;             // Not reply
      if (buf[6] != pending)
         return -4;
   }
   if (len < 2)
      return -6;                // Invalid
   len -= 2;
   int res = len;
   uint8_t sum = 0xD5 + pending;
   if (len > max1 + max2)
      return -7;                // Too big
   if (data1)
   {
      l = max1;
      if (l > len)
         l = len;
      if (l)
      {
         if (uart_rx (p, data1, l, 10) != l)
            return -8;          // Bad read
         len -= l;
         while (l)
            sum += data1[--l];
      }
   }
   if (data2)
   {
      l = max2;
      if (l > len)
         l = len;
      if (l)
      {
         if (uart_rx (p, data2, l, 10) != l)
            return -9;          // Bad read
         len -= l;
         while (l)
            sum += data2[--l];
      }
   }
   if (len)
      return -10;               // Uh?
   l = uart_rx (p, buf, 2, 10);
   if (l != 2)
      return -11;               // Postamble
   if ((uint8_t) (buf[0] + sum))
      return -12;               // checksum
   if (buf[1])
      return -13;               // postamble
   return res;
}

void
pn532_kick (pn532_t * p)
{                               // Kick if in VLowBat mode
   // Poke serial to wake up
   uint8_t buf[5];
   buf[0] = 0x55;
   buf[1] = 0x55;
   buf[2] = 0x00;
   buf[3] = 0x00;
   buf[4] = 0x00;
   uart_tx (p, buf, 5);
}

int
pn532_ready (pn532_t * p)
{
   if (!p->pending)
      return -1;                // Nothing pending
   size_t length;
   if (uart_get_buffered_data_len (p->uart, &length))
      return -2;                // Error
   return length;
}

// Data exchange (for DESFire use)
int
pn532_dx (pn532_t * p, unsigned int len, uint8_t * data, unsigned int max)
{                               // Card access function - sends to card starting CMD byte, and receives reply in to same buffer, starting status byte, returns len
   // TODO
   return -1;
}

// Other higher level functions
int
pn532_ILPT_Send (pn532_t * p)
{
   uint8_t buf[3];
   // InListPassiveTarget
   buf[0] = 2;                  // 2 tags (we only report 1)
   buf[1] = 0;                  // 106 kbps type A (ISO/IEC14443 Type A)
   int l = pn532_tx (p, 0x4A, 2, buf, 0, NULL);
   if (l < 0)
      return l;
   return 0;                    // Waiting
}

int
pn532_Cards (pn532_t * p)
{                               // -ve for error, else number of cards
   if (p->cards)
   {
      // TODO if we think in field, and that type of card, do an in field check and return answer
   }
   // InListPassiveTarget to get card count and baseID
   if (!p->pending)
      pn532_ILPT_Send (p);
   if (p->pending != 0x4B)
      return -1;                // We expect to be waiting for InListPassiveTarget response
   uint8_t buf[100];
   int l = pn532_rx (p, 1, buf, sizeof (buf), buf);
   if (l < 0)
      return l;
   if (l < 1)
      return -1;                // ?
   // Extract first card ID
   // TODO
   return p->cards = buf[0];
}
