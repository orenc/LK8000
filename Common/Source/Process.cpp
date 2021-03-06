/*
   LK8000 Tactical Flight Computer -  WWW.LK8000.IT
   Released under GNU/GPL License v.2
   See CREDITS.TXT file for authors and copyrights

   $Id: Process.cpp,v 8.3 2010/12/12 16:27:39 root Exp root $
*/

#include "StdAfx.h"
#include <windows.h>

#include "Defines.h" // VENTA3
#include "compatibility.h"
#ifdef OLDPPC
#include "LK8000Process.h"
#else
#include "Process.h"
#endif
#include "externs.h"

#include "AATDistance.h"

#include "utils/heapcheck.h"
using std::min;
using std::max;

extern AATDistance aatdistance;

extern int PDABatteryPercent;
extern int PDABatteryStatus;
extern int PDABatteryFlag;


void	WindDirectionProcessing(int UpDown)
{
	
	if(UpDown==1)
	{
		CALCULATED_INFO.WindBearing  += 5;
		while (CALCULATED_INFO.WindBearing  >= 360)
		{
			CALCULATED_INFO.WindBearing  -= 360;
		}
	}
	else if (UpDown==-1)
	{
		CALCULATED_INFO.WindBearing  -= 5;
		while (CALCULATED_INFO.WindBearing  < 0)
		{
			CALCULATED_INFO.WindBearing  += 360;
		}
	} else if (UpDown == 0) {
          SetWindEstimate(CALCULATED_INFO.WindSpeed,
                          CALCULATED_INFO.WindBearing);
	  #ifndef NOWINDREGISTRY
	  SaveWindToRegistry();
	  #endif
	}
	return;
}


void	WindSpeedProcessing(int UpDown)
{
	if(UpDown==1)
		CALCULATED_INFO.WindSpeed += (1/SPEEDMODIFY);
	else if (UpDown== -1)
	{
		CALCULATED_INFO.WindSpeed -= (1/SPEEDMODIFY);
		if(CALCULATED_INFO.WindSpeed < 0)
			CALCULATED_INFO.WindSpeed = 0;
	} 
	// JMW added faster way of changing wind direction
	else if (UpDown== -2) {
		WindDirectionProcessing(-1);
	} else if (UpDown== 2) {
		WindDirectionProcessing(1);
	} else if (UpDown == 0) {
          SetWindEstimate(CALCULATED_INFO.WindSpeed,
                          CALCULATED_INFO.WindBearing);
	  #ifndef NOWINDREGISTRY
	  SaveWindToRegistry();
	  #endif
	}
	return;
}

void	MacCreadyProcessing(int UpDown)
{

  if(UpDown==1) {
	CALCULATED_INFO.AutoMacCready=false;  // 091214 disable AutoMacCready when changing MC values
    MACCREADY += (double)0.1/LIFTMODIFY; // BUGFIX 100102
    
    if (MACCREADY>5.0) { // JMW added sensible limit
      MACCREADY=5.0;
    }
  }
  else if(UpDown==-1)
    {
	CALCULATED_INFO.AutoMacCready=false;  // 091214 disable AutoMacCready when changing MC values
      MACCREADY -= (double)0.1/LIFTMODIFY; // 100102
      if(MACCREADY < 0)
	{
	  MACCREADY = 0;
	}

  } else if (UpDown==0)
    {
      CALCULATED_INFO.AutoMacCready = !CALCULATED_INFO.AutoMacCready; 
      // JMW toggle automacready
	} 
  else if (UpDown==-2)
    {
      CALCULATED_INFO.AutoMacCready = false;  // SDP on auto maccready
      
    }
  else if (UpDown==+2)
    {
      CALCULATED_INFO.AutoMacCready = true;	// SDP off auto maccready
      
    }
  else if (UpDown==3)
    {
	CALCULATED_INFO.AutoMacCready=false;  // 091214 disable AutoMacCready when changing MC values
	MACCREADY += (double)0.5/LIFTMODIFY; // 100102
	if (MACCREADY>5.0) MACCREADY=5.0; 
      
    }
  else if (UpDown==-3)
    {
	CALCULATED_INFO.AutoMacCready=false;  // 091214 disable AutoMacCready when changing MC values
	MACCREADY -= (double)0.5/LIFTMODIFY; // 100102
	if (MACCREADY<0) MACCREADY=0; 
      
    }
  else if (UpDown==-5)
    {
      CALCULATED_INFO.AutoMacCready = true;
      AutoMcMode=amcFinalGlide;
    }
  else if (UpDown==-6)
    {
      CALCULATED_INFO.AutoMacCready = true;
      AutoMcMode=amcAverageClimb;
    }
  else if (UpDown==-7)
    {
      CALCULATED_INFO.AutoMacCready = true;
      AutoMcMode=amcEquivalent;
    }
  
  devPutMacCready(devA(), MACCREADY); 
  devPutMacCready(devB(), MACCREADY);
  
  return;
}

/*
	1	Next waypoint
	0	Show waypoint details
	-1	Previous waypoint
	2	Next waypoint with wrap around
	-2	Previous waypoint with wrap around
*/
void NextUpDown(int UpDown)
{

  if (!ValidTaskPoint(ActiveWayPoint)) {	// BUGFIX 091116
	StartupStore(_T(". DBG-801 activewaypoint%s"),NEWLINE);
	return;
  }

  LockTaskData();

  if(UpDown>0) {
    // this was a bug. checking if AWP was < 0 assuming AWP if inactive was -1; actually it can also be 0, a bug is around
    if(ActiveWayPoint < MAXTASKPOINTS) {
      // Increment Waypoint
      if(Task[ActiveWayPoint+1].Index >= 0) {
	if(ActiveWayPoint == 0)	{
	  // manual start
	  // TODO bug: allow restart
	  // TODO bug: make this work only for manual
	  if (CALCULATED_INFO.TaskStartTime==0) {
	    CALCULATED_INFO.TaskStartTime = GPS_INFO.Time;
	  }
	}
	ActiveWayPoint ++;
	AdvanceArmed = false;
	CALCULATED_INFO.LegStartTime = GPS_INFO.Time ;
      }
      // No more, try first
      else 
        if((UpDown == 2) && (Task[0].Index >= 0)) {
          /* ****DISABLED****
          if(ActiveWayPoint == 0)	{
            // TODO bug: allow restart
            // TODO bug: make this work only for manual
            
            // TODO bug: This should trigger reset of flight stats, but 
            // should ask first...
            if (CALCULATED_INFO.TaskStartTime==0) {
              CALCULATED_INFO.TaskStartTime = GPS_INFO.Time ;
            }            
          }
          */
          AdvanceArmed = false;
          ActiveWayPoint = 0;
          CALCULATED_INFO.LegStartTime = GPS_INFO.Time ;
        }
    }
  }
  else if (UpDown<0){
    if(ActiveWayPoint >0) {

      ActiveWayPoint --;
      /*
	XXX How do we know what the last one is?
	} else if (UpDown == -2) {
	ActiveWayPoint = MAXTASKPOINTS;
      */
    } else {
      if (ActiveWayPoint==0) {

        RotateStartPoints();

	// restarted task..
	//	TODO bug: not required? CALCULATED_INFO.TaskStartTime = 0;
      }
    }
    aatdistance.ResetEnterTrigger(ActiveWayPoint);    
  } 
  else if (UpDown==0) {
    SelectedWaypoint = Task[ActiveWayPoint].Index;
    PopupWaypointDetails();
  }
  if (ActiveWayPoint>=0) {
    SelectedWaypoint = Task[ActiveWayPoint].Index;
  }
  UnlockTaskData();
}


int TimeLocal(int localtime) {
  localtime += GetUTCOffset();
  if (localtime<0) {
    localtime+= 3600*24;
  }
  return localtime;
}

int DetectCurrentTime() {
  int localtime = (int)GPS_INFO.Time;
  return TimeLocal(localtime);
}

// simple localtime with no 24h exceeding
int LocalTime() {
  int localtime = (int)GPS_INFO.Time;
  localtime += GetUTCOffset();
  if (localtime<0) {
	localtime+= 86400;
  } else {
	if (localtime>86400) {
		localtime -=86400;
	}
  }
  return localtime;
}

int DetectStartTime(NMEA_INFO *Basic, DERIVED_INFO *Calculated) {
  // JMW added restart ability
  //
  // we want this to display landing time until next takeoff 

  static int starttime = -1;
  static int lastflighttime = -1;

  if (Calculated->Flying) {
    if (starttime == -1) {
      // hasn't been started yet
      
      starttime = (int)GPS_INFO.Time;

      lastflighttime = -1;
    }
    // bug? if Basic time rolled over 00 UTC, negative value is returned
    return (int)GPS_INFO.Time-starttime;

  } else {

    if (lastflighttime == -1) {
      // hasn't been stopped yet
      if (starttime>=0) {
	lastflighttime = (int)Basic->Time-starttime;
      } else {
	return 0; // no last flight time
      }
      // reset for next takeoff
      starttime = -1;
    }
  }

  // return last flighttime if it exists
  return max(0,lastflighttime);
}

