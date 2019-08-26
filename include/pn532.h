// PN532 functions

#ifndef	PN532_H
#define PN532_H

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <malloc.h>
#include "desfireaes.h"

#define pn532_errs		\
	p(OK)			\
	p(NULL)			\
	p(NOTPENDING)		\
	p(CMDPENDING)		\
	p(CMDMISMATCH)		\
	p(TIMEOUT)		\
	p(TIMEOUTACK)		\
	p(BADACK)		\
	p(NACK)			\
	p(HEADER)		\
	p(SHORT)		\
	p(SPACE)		\
	p(CHECKSUM)		\
	p(POSTAMBLE)		\
	p(STATUS)		\
	s(0x01,TIMEOUT)		\
	s(0x02,CRC)		\
	s(0x03,PARITY)		\
	s(0x04,BITCOUNT)	\
	s(0x05,FRAMING)		\
	s(0x06,COLLISION)	\
	s(0x07,SPACE)		\
	s(0x09,OVERFLOW)	\
	s(0x0A,NOFIELD)		\
	s(0x0B,PROTOCOL)	\
	s(0x0D,TEMPERATURE)	\
	s(0x0E,INTOVERFLOW)	\
	s(0x10,PARAMETER)	\
	s(0x12,DEPPROTOCOL)	\
	s(0x13,DEPFORMAT)	\
	s(0x14,MIFAREAUTH)	\
	s(0x23,UIDCHECK)	\
	s(0x25,DEPSTATE)	\
	s(0x26,NOTALLOWED)	\
	s(0x27,NOTACCEPTABLE)	\
	s(0x29,RELEASED)	\
	s(0x2A,CARDSWAPPED)	\
	s(0x2B,DISAPPEARED)	\
	s(0x2C,MISMATCHID)	\
	s(0x2D,OVERCURRENT)	\
	s(0x2E,NADMISSING)	\
	s(0x2F,MAX)		\

typedef enum
{
#define p(n)	PN532_ERR_##n,
#define s(v,n)	PN532_ERR_STATUS_##n=PN532_ERR_STATUS+v,
   pn532_errs
#undef p
#undef s
} pn532_err_t;

typedef struct pn532_s pn532_t;

// Functions

pn532_t *pn532_init (int uart, int tx, int rx, uint8_t p3);     // Init PN532 (P3 is port 3 output bits in use)
void *pn532_end (pn532_t * p);

pn532_err_t pn532_lasterr (pn532_t *);
const char *pn532_err_to_name (pn532_err_t);

// Low level access functions
int pn532_tx (pn532_t *, uint8_t cmd, int, uint8_t *, int, uint8_t *);  // Send data to PN532 (up to two blocks) return 0 or negative for error. Starts byte after cmd
int pn532_ready (pn532_t * p);  // >0 if response ready, 0 if not, -ve if error (e.g. no response expected)
int pn532_rx (pn532_t *, int, uint8_t *, int, uint8_t *);       // Recv data from PN532, (in to up to two blocks) return total length or -ve for error, checks res=cmd+1 and returns from byte after
uint8_t *pn532_nfcid (pn532_t *, char text[21]);        // Get NFCID (first byte is len of following)
uint8_t *pn532_ats (pn532_t *); // Get ATS (first byte is len of following - note, not as received were it is len inc the length byte)

// Card access function - sends to card starting CMD byte, and receives reply in to same buffer, starting status byte, returns len
int pn532_dx (void *, unsigned int len, uint8_t * data, unsigned int max, const char **errstr);

// Higher level useful PN532 functions
int pn532_write_P3 (pn532_t * p, uint8_t p3);
int pn532_read_P3 (pn532_t * p);
int pn532_ILPT_Send (pn532_t * p);      // Sets up InListPassiveTarget but does not wait for reply
int pn532_Cards (pn532_t * p);  // How many cards present
int pn532_Present (pn532_t * p);        // Check if present still


#endif
