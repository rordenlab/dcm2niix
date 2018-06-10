//
// Copyright CharLS Team, all rights reserved. See the accompanying "License.txt" for licensed use.
//

#ifndef CHARLS_CONSTANTS
#define CHARLS_CONSTANTS

// Default threshold values for JPEG-LS statistical modeling as defined in ISO/IEC 14495-1, Table C.3
// for the case MAXVAL = 255 and NEAR = 0.
// Can be overridden at compression time, however this is rarely done.
const int DefaultThreshold1 = 3;  // BASIC_T1
const int DefaultThreshold2 = 7;  // BASIC_T2
const int DefaultThreshold3 = 21; // BASIC_T3

const int DefaultResetValue = 64; // Default RESET value as defined in  ISO/IEC 14495-1, table C.2

#endif
