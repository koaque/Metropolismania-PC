/* Base types for the game's structs, parsed into Ghidra by tools/ghidra/ApplyTypes.java.
   Plain C declarations only: preprocessor lines are skipped by the parser.
   Guest view: pointers are 32 bits, as on the PS2's EE. */

typedef unsigned char uint8_t;
typedef signed char int8_t;
typedef unsigned short uint16_t;
typedef short int16_t;
typedef unsigned int uint32_t;
typedef int int32_t;
typedef unsigned long long uint64_t;
typedef long long int64_t;
