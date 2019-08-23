// PN532 functions

#ifndef	PN532_H
#define PN532_H

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <malloc.h>
#include "../DESFireAES/desfireaes.h"

typedef struct pn532_s pn532_t;

pn532_t *pn532_init(int uart,int tx,int rx,uint8_t p3); // Init PN532 (P3 is port 3 output bits in use)
void *pn532_end(pn532_t *p);

// Low level access functions
int pn532_tx(pn532_t*,int ,uint8_t *,int ,uint8_t *);	// Send data to PN532 (up to two blocks) return 0 or negative for error
int pn532_rx(pn532_t*,int ,uint8_t *,int ,uint8_t *);	// Recv data from PN532, (in to up to two blocks) return total length or -ve for error

// Card access function - sends to card starting CMD byte, and receives reply in to same buffer, starting status byte, returns len
int pn532_dx (pn532_t*, unsigned int len, uint8_t *data, unsigned int max);

// Higher level useful PN532 functions


#endif
