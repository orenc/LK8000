/*
   LK8000 Tactical Flight Computer -  WWW.LK8000.IT
   Released under GNU/GPL License v.2
   See CREDITS.TXT file for authors and copyrights

   $Id$
*/

#if !defined(BITMAPS_H)
#define BITMAPS_H

#if defined(STATIC_BITMAPS)
  #define BEXTMODE 
  #undef  STATIC_BITMAPS
  #define BEXTNULL	=NULL

#else
  #undef  BEXTMODE
  #define BEXTMODE extern
  #define BEXTNULL

  extern void LKLoadFixedBitmaps(void);
  extern void LKLoadProfileBitmaps(void);
  extern void LKUnloadFixedBitmaps(void);
  extern void LKUnloadProfileBitmaps(void);
#endif

//
// FIXED BITMAPS
//
BEXTMODE  HBITMAP hTurnPoint BEXTNULL;
BEXTMODE  HBITMAP hInvTurnPoint BEXTNULL;
BEXTMODE  HBITMAP hSmall BEXTNULL;
BEXTMODE  HBITMAP hInvSmall BEXTNULL;
BEXTMODE  HBITMAP hTerrainWarning BEXTNULL;
BEXTMODE  HBITMAP hAirspaceWarning BEXTNULL;
BEXTMODE  HBITMAP hLogger BEXTNULL;
BEXTMODE  HBITMAP hFLARMTraffic BEXTNULL;
BEXTMODE  HBITMAP hBatteryFull BEXTNULL;
BEXTMODE  HBITMAP hBattery70 BEXTNULL;
BEXTMODE  HBITMAP hBattery50 BEXTNULL;
BEXTMODE  HBITMAP hBattery25 BEXTNULL;
BEXTMODE  HBITMAP hBattery15 BEXTNULL;

BEXTMODE  HBITMAP hBmpThermalSource BEXTNULL;
BEXTMODE  HBITMAP hBmpTarget BEXTNULL;
BEXTMODE  HBITMAP hBmpMarker BEXTNULL;
BEXTMODE  HBITMAP hBmpTeammatePosition BEXTNULL;
BEXTMODE  HBITMAP hAboveTerrainBitmap BEXTNULL;

BEXTMODE  HBITMAP hAirspaceBitmap[NUMAIRSPACEBRUSHES];

BEXTMODE  HBITMAP hBmpLeft32 BEXTNULL;
BEXTMODE  HBITMAP hBmpRight32 BEXTNULL;
BEXTMODE  HBITMAP hScrollBarBitmapTop BEXTNULL;
BEXTMODE  HBITMAP hScrollBarBitmapMid BEXTNULL;
BEXTMODE  HBITMAP hScrollBarBitmapBot BEXTNULL;

//
// PROFILE DEPENDENT BITMAPS
//
BEXTMODE  HBITMAP hBmpAirportReachable BEXTNULL;
BEXTMODE  HBITMAP hBmpAirportUnReachable BEXTNULL;
BEXTMODE  HBITMAP hBmpFieldReachable BEXTNULL;
BEXTMODE  HBITMAP hBmpFieldUnReachable BEXTNULL;
BEXTMODE  HBITMAP hCruise BEXTNULL;
BEXTMODE  HBITMAP hClimb BEXTNULL;
BEXTMODE  HBITMAP hFinalGlide BEXTNULL;

#endif
