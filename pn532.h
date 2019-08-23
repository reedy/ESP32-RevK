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

pn532_t *pn532_init (int uart, int tx, int rx, uint8_t p3);     // Init PN532 (P3 is port 3 output bits in use)
void *pn532_end (pn532_t * p);

// Low level access functions
void pn532_kick (pn532_t * p);  // Wake up from VLowBat mode
int pn532_tx (pn532_t *,uint8_t cmd, int, uint8_t *, int, uint8_t *);       // Send data to PN532 (up to two blocks) return 0 or negative for error. Starts byte after cmd
int pn532_ready (pn532_t * p);  // >0 if response ready, 0 if not, -ve if error (e.g. no response expected)
int pn532_rx (pn532_t *, int, uint8_t *, int, uint8_t *);       // Recv data from PN532, (in to up to two blocks) return total length or -ve for error, checks res=cmd+1 and returns from byte after
uint8_t *pn532_nfcid(pn532_t*);	// Get NFCID (first byte is len of following)
uint8_t *pn532_ats(pn532_t*);	// Get ATS (first byte is len of following - note, not as received were it is len inc the length byte)

// Card access function - sends to card starting CMD byte, and receives reply in to same buffer, starting status byte, returns len
int pn532_dx (pn532_t *, unsigned int len, uint8_t * data, unsigned int max);

// Higher level useful PN532 functions
int pn532_ILPT_Send (pn532_t * p);      // Sets up InListPassiveTarget but does not wait for reply
int pn532_Cards (pn532_t * p);  // How many cards present


#endif
