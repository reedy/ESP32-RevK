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
int pn532_send(pn532_t*,int len,uint8_t *data);	// Send data to PN532
int pn532_recv(pn532_t*,int max,uint8_t *data);	// Recv data from PN532

// Card access function - sends to card starting CMD byte, and receives reply in to same buffer, starting status byte, returns len
int pn532_dx (pn532_t*, unsigned int len, uint8_t *data, unsigned int max);

// Higher level useful PN532 functions


#endif
