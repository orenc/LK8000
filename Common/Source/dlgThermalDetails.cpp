/*
   LK8000 Tactical Flight Computer -  WWW.LK8000.IT
   Released under GNU/GPL License v.2
   See CREDITS.TXT file for authors and copyrights

   $Id$
*/

#include "StdAfx.h"
#include <aygshell.h>
#include "lk8000.h"
#include "Statistics.h"
#include "externs.h"
#include "dlgTools.h"
#include "InfoBoxLayout.h"
#include "Utils2.h"
#include "NavFunctions.h"
#include "TeamCodeCalculation.h"

#include "utils/heapcheck.h"

static WndForm *wf=NULL;
static void SetValues(int indexid);

static int s_selected;

static void OnSelectClicked(WindowControl * Sender) {
  (void)Sender;

  if (s_selected<0 || s_selected>=MAXTHISTORY) {
	StartupStore(_T("... Invalid thermal selected to multitarget, out of range:%d %s"),s_selected,NEWLINE);
	DoStatusMessage(_T("ERR-126 invalid thermal"));
	return;
  }

  if (!ThermalHistory[s_selected].Valid) {
	DoStatusMessage(gettext(TEXT("ERR-127 invalid thermal selection")));
	return;
  }

  #if TESTBENCH
  StartupStore(_T("... Selected thermal n.%d <%s>\n"),s_selected,ThermalHistory[s_selected].Name);
  #endif

  SetThermalMultitarget(s_selected); // update selected multitarget

  LockTaskData();
  // now select the new one

  WayPointList[RESWP_LASTTHERMAL].Latitude  = ThermalHistory[s_selected].Latitude;
  WayPointList[RESWP_LASTTHERMAL].Longitude = ThermalHistory[s_selected].Longitude;
  WayPointList[RESWP_LASTTHERMAL].Altitude  = ThermalHistory[s_selected].HBase;
  
  wcscpy(WayPointList[RESWP_LASTTHERMAL].Name, ThermalHistory[s_selected].Name);

  UnlockTaskData();

  // switch to L> multitarget, and force moving map mode
  OvertargetMode=OVT_THER;
  SetModeType(LKMODE_MAP,MP_MOVING);

  wf->SetModalResult(mrOK);

}

static void OnCloseClicked(WindowControl * Sender){
	(void)Sender;
  wf->SetModalResult(mrOK);
}

static CallBackTableEntry_t CallBackTable[]={
  DeclareCallBackEntry(OnCloseClicked),
  DeclareCallBackEntry(OnSelectClicked),
  DeclareCallBackEntry(NULL)
};


static void SetValues(int indexid) {
  WndProperty* wp;
  TCHAR buffer[80];

  if (indexid<0 || indexid>MAXTHISTORY) { // TODO check
	StartupStore(_T("... LK thermal setvalues invalid indexid=%d%s"),indexid,NEWLINE);
	DoStatusMessage(_T("ERR-216 INVALID THERMAL INDEXID"));
	return;
  }
  if ( !ThermalHistory[indexid].Valid ) {
	DoStatusMessage(_T("ERR-217 INVALID THERMAL INDEXID"));
	return;
  }

  wp = (WndProperty*)wf->FindByName(TEXT("prpName"));
  if (wp) {
	wcscpy(buffer,ThermalHistory[indexid].Name);
	ConvToUpper(buffer);
	wp->SetText(buffer);
	wp->RefreshDisplay();
  }

  wp = (WndProperty*)wf->FindByName(TEXT("prpHTop"));
  if (wp) {
	_stprintf(buffer,_T("%.0f %s"),ThermalHistory[indexid].HTop*ALTITUDEMODIFY, Units::GetAltitudeName());
	wp->SetText(buffer);
	wp->RefreshDisplay();
  }

  wp = (WndProperty*)wf->FindByName(TEXT("prpHBase"));
  if (wp) {
	_stprintf(buffer,_T("%.0f %s"),ThermalHistory[indexid].HBase*ALTITUDEMODIFY, Units::GetAltitudeName());
	wp->SetText(buffer);
	wp->RefreshDisplay();
  }

  wp = (WndProperty*)wf->FindByName(TEXT("prpLift"));
  if (wp) {
	_stprintf(buffer,_T("%+.1f %s"),ThermalHistory[indexid].Lift*LIFTMODIFY, Units::GetVerticalSpeedName());
	wp->SetText(buffer);
	wp->RefreshDisplay();
  }

  wp = (WndProperty*)wf->FindByName(TEXT("prpTeamCode"));
  if (wp) {
	// Taken from CalculateTeamBear..
	if (!WayPointList) return;
	if (TeamCodeRefWaypoint < 0) return;

	double distance=0, bearing=0;

	LL_to_BearRange( WayPointList[TeamCodeRefWaypoint].Latitude,
           WayPointList[TeamCodeRefWaypoint].Longitude,
           ThermalHistory[indexid].Latitude,
           ThermalHistory[indexid].Longitude,
           &bearing, &distance);

	GetTeamCode(buffer, bearing, distance);

	buffer[5]='\0';
	wp->SetText(buffer);
	wp->RefreshDisplay();
  }

}


void dlgThermalDetails(int indexid) {

  char filename[MAX_PATH];
  LocalPathS(filename, TEXT("dlgThermalDetails.xml"));
  wf = dlgLoadFromXML(CallBackTable,
		      filename, 
		      hWndMainWindow,
		      TEXT("IDR_XML_THERMALDETAILS"));

  if (!wf) return;

  SetValues(indexid);

  s_selected=indexid;

  if (wcslen(ThermalHistory[indexid].Near) >0) {
	TCHAR tcap[100];
	wsprintf(tcap,_T("%s %s: %s"),
		gettext(_T("_@M905_")), // Thermal
		gettext(_T("_@M456_")), // Near
		ThermalHistory[indexid].Near
	);
	wf->SetCaption(tcap);
  }

  wf->ShowModal();

  delete wf;
  wf = NULL;
  return;
}

