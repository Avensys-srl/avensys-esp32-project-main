/*******************************************************************************
  System Definitions

  File Name:
    definitions.h

  Summary:
    project system definitions.

  Description:
    This file contains the system-wide prototypes and definitions for a project.

 *******************************************************************************/
#ifndef DEFINITIONS_H
#define DEFINITIONS_H

// *****************************************************************************
// Section: Included Files
// *****************************************************************************
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* CPU clock frequency */
#define byte                      uint8_t
#define word                      uint16_t
#define CHECK_BIT(var, pos)       ((var) & (1 << (pos)))
#define lowByte(w)                ((uint8_t)((w)&0xff))
#define highByte(w)               ((uint8_t)((w) >> 8))
#define min(a, b)                 ((a) < (b) ? (a) : (b))
#define max(a, b)                 ((a) > (b) ? (a) : (b))
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
// #define abs(x)                    ((x) > 0 ? (x) : -(x))

#define bitRead(value, bit)            (((value) >> (bit)) & 0x01)
#define bitSet(value, bit)             ((value) |= (1UL << (bit)))
#define bitClear(value, bit)           ((value) &= ~(1UL << (bit)))
#define bitWrite(value, bit, bitvalue) (bitvalue ? bitSet(value, bit) : bitClear(value, bit))

#define MAKEWORD( _Buffer, _HiIndex, _LoIndex ) ((word) (_Buffer[ _HiIndex ] << 8) | (word) _Buffer[ _LoIndex ])
#define MAKEINT( _Buffer, _HiIndex, _LoIndex ) ((int) (_Buffer[ _HiIndex ] << 8) | (int) _Buffer[ _LoIndex ])
#define MAKESHORT( _Buffer, _HiIndex, _LoIndex ) ((short) (_Buffer[ _HiIndex ] << 8) | (short) _Buffer[ _LoIndex ])
#define MAX( n1, n2 ) (n1 > n2 ? n1 : n2)
#define MIN( n1, n2 ) (n1 < n2 ? n1 : n2)

#define GET2BITVALUE( value, index ) ((value >> (index << 1)) & 0x03)
#define SET2BITVALUE( value, index, bitValue ) (value & ~(0x03 << (index * 2))) | (bitValue << (index * 2))

#endif /* DEFINITIONS_H */
/*******************************************************************************
 End of File
*/
