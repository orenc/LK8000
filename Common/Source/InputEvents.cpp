/*
   LK8000 Tactical Flight Computer -  WWW.LK8000.IT
   Released under GNU/GPL License v.2
   See CREDITS.TXT file for authors and copyrights

  $Id: InputEvents.cpp,v 8.11 2011/01/01 23:21:36 root Exp root $
*/


#include "StdAfx.h"
#include "lk8000.h"
#include "InputEvents.h"
#include "Utils.h"
#include "Utils2.h"
#include "Dialogs.h"
#include "Terrain.h"
#include "compatibility.h"
#include <commctrl.h>
#include <aygshell.h>
#include "InfoBoxLayout.h"
#include "Airspace.h"
#include "LKAirspace.h"
using std::min;
using std::max;
#ifdef OLDPPC
#include "LK8000Process.h"
#else
#include "Process.h"
#endif
#include "device.h"
#include "Message.h"
#include "Units.h"
#include "MapWindow.h"
#include "Atmosphere.h"
#include "Waypointparser.h"

#include "utils/stringext.h"
#include "utils/heapcheck.h"

// Sensible maximums 
#define MAX_MODE 100
#define MAX_MODE_STRING 25
#define MAX_KEY 255
#define MAX_EVENTS 2048
#define MAX_LABEL NUMBUTTONLABELS

extern short TerrainRamp;

#define ISPARAGLIDER (AircraftCategory == (AircraftCategory_t)umParaglider)

// Current modes - map mode to integer (primitive hash)
static TCHAR mode_current[MAX_MODE_STRING] = TEXT("default");		// Current mode
static TCHAR mode_map[MAX_MODE][MAX_MODE_STRING];					// Map mode to location
static int mode_map_count = 0;

// Key map to Event - Keys (per mode) mapped to events
static int Key2Event[MAX_MODE][MAX_KEY];		// Points to Events location

// Glide Computer Events
static int GC2Event[MAX_MODE][GCE_COUNT];

// NMEA Triggered Events
static int N2Event[MAX_MODE][NE_COUNT];

// Events - What do you want to DO
typedef struct {
  pt2Event event; // Which function to call (can be any, but should be here)
  TCHAR *misc;    // Parameters
  int next;       // Next in event list - eg: Macros
} EventSTRUCT;

static EventSTRUCT Events[MAX_EVENTS];	
static int Events_count;				// How many have we defined

// Labels - defined per mode
typedef struct {
  const TCHAR *label;
  int location;
  int event;
} ModeLabelSTRUCT;
static ModeLabelSTRUCT ModeLabel[MAX_MODE][MAX_LABEL];
static int ModeLabel_count[MAX_MODE];	       // Where are we up to in this mode...


#define MAX_GCE_QUEUE 10
static int GCE_Queue[MAX_GCE_QUEUE];
#define MAX_NMEA_QUEUE 10
static int NMEA_Queue[MAX_NMEA_QUEUE];


// -----------------------------------------------------------------------
// Initialisation and Defaults
// -----------------------------------------------------------------------

bool InitONCE = false;

// Mapping text names of events to the real thing
typedef struct {
  const TCHAR *text;
  pt2Event event;
} Text2EventSTRUCT;
Text2EventSTRUCT Text2Event[256];  // why 256?
int Text2Event_count;

// Mapping text names of events to the real thing
const TCHAR *Text2GCE[GCE_COUNT+1];

// Mapping text names of events to the real thing
const TCHAR *Text2NE[NE_COUNT+1];

// DLL Cache
typedef void (CALLBACK *DLLFUNC_INPUTEVENT)(TCHAR*);
typedef void (CALLBACK *DLLFUNC_SETHINST)(HMODULE);


#define MAX_DLL_CACHE 256
typedef struct {
  TCHAR *text;
  HINSTANCE hinstance;
} DLLCACHESTRUCT;
DLLCACHESTRUCT DLLCache[MAX_DLL_CACHE];
int DLLCache_Count = 0;

// Read the data files
void InputEvents::readFile() {
  StartupStore(TEXT(". Loading input events file%s"),NEWLINE);

  // clear the GCE and NMEA queues
  LockEventQueue();
  int i;
  for (i=0; i<MAX_GCE_QUEUE; i++) {
    GCE_Queue[i]= -1;
  }
  for (i=0; i<MAX_NMEA_QUEUE; i++) {
    NMEA_Queue[i]= -1;
  }
  UnlockEventQueue();

  // Get defaults 
  if (!InitONCE) {
	#include "InputEvents_LK8000.cpp"  
	#include "InputEvents_Text2Event.cpp"
    InitONCE = true;
  }

  // Read in user defined configuration file
	
  TCHAR szFile1[MAX_PATH] = TEXT("\0");
  ZZIP_FILE *fp=NULL;

  // Open file from registry
  // This is used by LK engineering mode only, and has priority
  GetRegistryString(szRegistryInputFile, szFile1, MAX_PATH);
  ExpandLocalPath(szFile1);
  SetRegistryString(szRegistryInputFile, TEXT("\0"));
	
  if (_tcslen(szFile1)>0) {
    fp=zzip_fopen(szFile1, "rb");
  }

  TCHAR xcifile[MAX_PATH];
  if (fp == NULL) {
	// no special XCI in engineering, or nonexistent file.. go ahead with language check

	// if ( _tcslen(LKLangSuffix)<3) return; // No more needed since DEFAULT
	TCHAR xcipath[MAX_PATH];
	LocalPath(xcipath,_T(LKD_SYSTEM));
	// DEFAULT_MENU existance is checked upon startup.
	// In the near future we can change it dynamically during setup, depending on
	// LK usage mode.
	_stprintf(xcifile,_T("%s\\DEFAULT_MENU.TXT"), xcipath);
	fp=zzip_fopen(xcifile, "rb");
	if (fp == NULL) {
		StartupStore(_T(". No menu <%s>, using internal XCI\n"),xcifile);
		return;
	} else
		StartupStore(_T(". Loaded menu <%s>\n"),xcifile);
  }

  // TODO code - Safer sizes, strings etc - use C++ (can scanf restrict length?)
  TCHAR buffer[2049];	// Buffer for all
  TCHAR key[2049];	// key from scanf
  TCHAR value[2049];	// value from scanf
  TCHAR *new_label = NULL;		
  int found = 0;

  // Init first entry
  bool some_data = false;		// Did we fin some in the last loop...
  TCHAR d_mode[1024] = TEXT("");			// Multiple modes (so large string)
  TCHAR d_type[256] = TEXT("");
  TCHAR d_data[256] = TEXT("");
  int event_id = 0;
  TCHAR d_label[256] = TEXT("");
  int d_location = 0;
  TCHAR d_event[256] = TEXT("");
  TCHAR d_misc[256] = TEXT("");

  int line = 0;

  /* Read from the file */
  // TODO code: Note that ^# does not allow # in key - might be required (probably not)
  //		Better way is to separate the check for # and the scanf
  // ! _stscanf works differently on WinPC and WinCE (on WinCE it returns EOF on empty string)

  while (ReadULine(fp, buffer, countof(buffer)) && (buffer[0] == '\0' ||
	   ((found = _stscanf(buffer, TEXT("%[^#=]=%[^\r\n][\r\n]"), key, value)) != EOF))
  ) {
    line++;

    // experimental: if the first line is "#CLEAR" then the whole default config is cleared
    //               and can be overwritten by file  
    if ((line == 1) && (_tcsstr(buffer, TEXT("#CLEAR")))){
      memset(&Key2Event, 0, sizeof(Key2Event));
      memset(&GC2Event, 0, sizeof(GC2Event));
      memset(&Events, 0, sizeof(Events));
      memset(&ModeLabel, 0, sizeof(ModeLabel));
      memset(&ModeLabel_count, 0, sizeof(ModeLabel_count));
      Events_count = 0;
    }

    // Check valid line? If not valid, assume next record (primative, but works ok!)
    if ((buffer[0] == '\r') || (buffer[0] == '\n') || (buffer[0] == '\0')) {
      // General checks before continue...
      if (
	  some_data
	  && (d_mode != NULL)						// We have a mode
	  && (_tcscmp(d_mode, TEXT("")) != 0)		//
	  ) {

	TCHAR *token;

	// For each mode
	token = _tcstok(d_mode, TEXT(" "));

	// General errors - these should be true
	ASSERT(d_location >= 0);
	ASSERT(d_location < 1024);	// Scott arbitrary limit
	ASSERT(event_id >= 0);
	ASSERT(d_mode != NULL);
	ASSERT(d_type != NULL);
	ASSERT(d_label != NULL);

	// These could indicate bad data - thus not an ASSERT (debug only)
	// ASSERT(_tcslen(d_mode) < 1024);
	// ASSERT(_tcslen(d_type) < 1024);
	// ASSERT(_tcslen(d_label) < 1024);

	while( token != NULL ) {

	  // All modes are valid at this point
	  int mode_id = mode2int(token, true);
	  ASSERT(mode_id >= 0);
			  
	  // Make label event
	  // TODO code: Consider Reuse existing entries...
	  if (d_location > 0) {
	    // Only copy this once per object - save string space
	    if (!new_label) {
	      new_label = StringMallocParse(d_label);
	    }
	    InputEvents::makeLabel(mode_id, new_label, d_location, event_id);
	  } 

	  // Make key (Keyboard input)
	  if (_tcscmp(d_type, TEXT("key")) == 0)	{	// key - Hardware key or keyboard
	    int key = findKey(d_data);				// Get the int key (eg: APP1 vs 'a')
	    if (key > 0)
	      Key2Event[mode_id][key] = event_id;
			    
	    // Make gce (Glide Computer Event)
	  } else if (_tcscmp(d_type, TEXT("gce")) == 0) {		// GCE - Glide Computer Event
	    int key = findGCE(d_data);				// Get the int key (eg: APP1 vs 'a')
	    if (key >= 0)
	      GC2Event[mode_id][key] = event_id;
			    
	    // Make ne (NMEA Event)
	  } else if (_tcscmp(d_type, TEXT("ne")) == 0) { 		// NE - NMEA Event
	    int key = findNE(d_data);			// Get the int key (eg: APP1 vs 'a')
	    if (key >= 0)
	      N2Event[mode_id][key] = event_id;
	  } else if (_tcscmp(d_type, TEXT("label")) == 0)	{	// label only - no key associated (label can still be touch screen)
	    // Nothing to do here...
			    
	  }
			  
	  token = _tcstok( NULL, TEXT(" "));
	}
			
      }
		
      // Clear all data.
      some_data = false;
      _tcscpy(d_mode, TEXT(""));
      _tcscpy(d_type, TEXT(""));
      _tcscpy(d_data, TEXT(""));
      event_id = 0;
      _tcscpy(d_label, TEXT(""));
      d_location = 0;
      new_label = NULL;

    } else if ((found != 2) || key[0] == '\0' || value[0] == '\0') {
      // Do nothing - we probably just have a comment line
      // JG removed "void;" - causes warning (void is declaration and needs variable)
      // NOTE: Do NOT display buffer to user as it may contain an invalid stirng !

    } else {
      if (_tcscmp(key, TEXT("mode")) == 0) {
	if (_tcslen(value) < 1024) {
	  some_data = true;	// Success, we have a real entry
	  _tcscpy(d_mode, value);				
	}
      } else if (_tcscmp(key, TEXT("type")) == 0) {
	if (_tcslen(value) < 256)
	  _tcscpy(d_type, value);				
      } else if (_tcscmp(key, TEXT("data")) == 0) {
	if (_tcslen(value) < 256)
	  _tcscpy(d_data, value);				
      } else if (_tcscmp(key, TEXT("event")) == 0) {
	if (_tcslen(value) < 256) {
	  _tcscpy(d_event, TEXT(""));
	  _tcscpy(d_misc, TEXT(""));
	  int ef;
	    ef = _stscanf(value, TEXT("%[^ ] %[A-Za-z0-9 \\/().,]"), d_event, d_misc);

	  // TODO code: Can't use token here - breaks
	  // other token - damn C - how about
	  // C++ String class ?
	
	  // TCHAR *eventtoken;
	  // eventtoken = _tcstok(value, TEXT(" "));
	  // d_event = token;
	  // eventtoken = _tcstok(value, TEXT(" "));

	  if ((ef == 1) || (ef == 2)) {
	  
	    // TODO code: Consider reusing existing identical events
	  
	    pt2Event event = findEvent(d_event);
	    if (event) {
	      event_id = makeEvent(event, 
                                   StringMallocParse(d_misc), event_id);
	    }
	  }
	}
      } else if (_tcscmp(key, TEXT("label")) == 0) {
	_tcscpy(d_label, value);				
      } else if (_tcscmp(key, TEXT("location")) == 0) {
	_stscanf(value, TEXT("%d"), &d_location);
	
      }
    }
	
  } // end while

  // file was ok, so save it to registry
  ContractLocalPath(szFile1);
  SetRegistryString(szRegistryInputFile, szFile1);

  zzip_fclose(fp);
}


int InputEvents::findKey(const TCHAR *data) {

  if (_tcscmp(data, TEXT("APP1")) == 0)
    return VK_APP1;
  else if (_tcscmp(data, TEXT("APP2")) == 0)
    return VK_APP2;
  else if (_tcscmp(data, TEXT("APP3")) == 0)
    return VK_APP3;
  else if (_tcscmp(data, TEXT("APP4")) == 0)
    return VK_APP4;
  else if (_tcscmp(data, TEXT("APP5")) == 0)
    return VK_APP5;
  else if (_tcscmp(data, TEXT("APP6")) == 0)
    return VK_APP6;
  else if (_tcscmp(data, TEXT("F1")) == 0)
    return VK_F1;
  else if (_tcscmp(data, TEXT("F2")) == 0)
    return VK_F2;
  else if (_tcscmp(data, TEXT("F3")) == 0)
    return VK_F3;
  else if (_tcscmp(data, TEXT("F4")) == 0)
    return VK_F4;
  else if (_tcscmp(data, TEXT("F5")) == 0)
    return VK_F5;
  else if (_tcscmp(data, TEXT("F6")) == 0)
    return VK_F6;
  else if (_tcscmp(data, TEXT("F7")) == 0)
    return VK_F7;
  else if (_tcscmp(data, TEXT("F8")) == 0)
    return VK_F8;
  else if (_tcscmp(data, TEXT("F9")) == 0)
    return VK_F9;
  else if (_tcscmp(data, TEXT("F10")) == 0)
    return VK_F10;
// VENTA-TEST HANDLING EXTRA HW KEYS ON HX4700 and HP31X
//  else if (_tcscmp(data, TEXT("F11")) == 0)
//  return VK_F11;
// else if (_tcscmp(data, TEXT("F12")) == 0)
//    return VK_F12;
  else if (_tcscmp(data, TEXT("LEFT")) == 0)
    return VK_LEFT;
  else if (_tcscmp(data, TEXT("RIGHT")) == 0)
    return VK_RIGHT;
  else if (_tcscmp(data, TEXT("UP")) == 0)
    return VK_UP;
  else if (_tcscmp(data, TEXT("DOWN")) == 0) {
    return VK_DOWN;
		}
  else if (_tcscmp(data, TEXT("RETURN")) == 0)
    return VK_RETURN;
  else if (_tcscmp(data, TEXT("ESCAPE")) == 0)
    return VK_ESCAPE;

  else if (_tcslen(data) == 1)
    return towupper(data[0]);
  else
    return 0;
  
}

pt2Event InputEvents::findEvent(const TCHAR *data) {
  int i;
  for (i = 0; i < Text2Event_count; i++) {
    if (_tcscmp(data, Text2Event[i].text) == 0)
      return Text2Event[i].event;
  }
  return NULL;
}

int InputEvents::findGCE(const TCHAR *data) {
  int i;
  for (i = 0; i < GCE_COUNT; i++) {
    if (_tcscmp(data, Text2GCE[i]) == 0)
      return i;
  }
  return -1;
}

int InputEvents::findNE(const TCHAR *data) {
  int i;
  for (i = 0; i < NE_COUNT; i++) {
    if (_tcscmp(data, Text2NE[i]) == 0)
      return i;
  }
  return -1;
}

// Create EVENT Entry
// NOTE: String must already be copied (allows us to use literals
// without taking up more data - but when loading from file must copy string
int InputEvents::makeEvent(void (*event)(const TCHAR *), const TCHAR *misc, int next) {
  if (Events_count >= MAX_EVENTS){
    ASSERT(0);
    return 0;
  }
  Events_count++;	// NOTE - Starts at 1 - 0 is a noop
  Events[Events_count].event = event;
  Events[Events_count].misc = (TCHAR*)misc;
  Events[Events_count].next = next;
  
  return Events_count;
}


// Make a new label (add to the end each time)
// NOTE: String must already be copied (allows us to use literals
// without taking up more data - but when loading from file must copy string
void InputEvents::makeLabel(int mode_id, const TCHAR* label, int location, int event_id) {

  if ((mode_id >= 0) && (mode_id < MAX_MODE) && (ModeLabel_count[mode_id] < MAX_LABEL)) {
    ModeLabel[mode_id][ModeLabel_count[mode_id]].label = label;
    ModeLabel[mode_id][ModeLabel_count[mode_id]].location = location;
    ModeLabel[mode_id][ModeLabel_count[mode_id]].event = event_id;
    ModeLabel_count[mode_id]++;
  } else {
	ASSERT(0);
  }
}

// Return 0 for anything else - should probably return -1 !
int InputEvents::mode2int(const TCHAR *mode, bool create) {
  int i = 0;
  
  // Better checks !
  if ((mode == NULL))
    return -1;
  
  for (i = 0; i < mode_map_count; i++) {
    if (_tcscmp(mode, mode_map[i]) == 0)
      return i;
  }
  
  if (create) {
    // Keep a copy
    _tcsncpy(mode_map[mode_map_count], mode, MAX_MODE_STRING);
    mode_map[mode_map_count][MAX_MODE_STRING-1]='\0'; // BUGFIX 100331 AND 110202
    mode_map_count++;
    return mode_map_count - 1;
  }

  // Should never reach this point
  ASSERT(false);
  return -1;
}


void InputEvents::setMode(const TCHAR *mode) {
  static int lastmode = -1;
  int thismode;

  ASSERT(mode != NULL);

  _tcsncpy(mode_current, mode, MAX_MODE_STRING);
  mode_current[MAX_MODE_STRING-1]='\0'; // BUGFIX 100331 AND 110202

  // Mode must already exist to use it here...
  thismode = mode2int(mode,false);
  if (thismode < 0)	// Technically an error in config (eg
			// event=Mode DoesNotExist)
    return;	// TODO enhancement: Add debugging here

  if (thismode == lastmode) return;

  ButtonLabel::SetLabelText(0,NULL);

  drawButtons(thismode);

  lastmode = thismode;

}

void InputEvents::drawButtons(int Mode){
  int i;

  if (!(ProgramStarted==psNormalOp)) return;

  for (i = 0; i < ModeLabel_count[Mode]; i++) {
    if ((ModeLabel[Mode][i].location > 0)) {

      ButtonLabel::SetLabelText(
        ModeLabel[Mode][i].location,
        ModeLabel[Mode][i].label
      );

    }
  }

  MapWindow::RequestFastRefresh();

}

TCHAR* InputEvents::getMode() {
  return mode_current;
}

int InputEvents::getModeID() {
  return InputEvents::mode2int(InputEvents::getMode(), false);
}

// -----------------------------------------------------------------------
// Processing functions - which one to do
// -----------------------------------------------------------------------


// Input is a via the user touching the label on a touch screen / mouse
bool InputEvents::processButton(int bindex) {
  if (!(ProgramStarted==psNormalOp)) return false;

  int thismode = getModeID();

  int i;
  // Note - reverse order - last one wins
  for (i = ModeLabel_count[thismode]; i >= 0; i--) {
	if ((ModeLabel[thismode][i].location == bindex) ) {

		int lastMode = thismode;

		// JMW need a debounce method here..
		if (!Debounce()) return true;

		// 101212 moved here so that an internal resource played will not stop LKsound running
		#ifndef DISABLEAUDIO
		if (EnableSoundModes) PlayResource(TEXT("IDR_WAV_CLICK"));
		#endif

		if (!ButtonLabel::ButtonDisabled[bindex]) {
			#if 0 // REMOVE ANIMATION
			ButtonLabel::AnimateButton(bindex);
			#endif
			processGo(ModeLabel[thismode][i].event);
		}

		// update button text, macro may change the label
		if ((lastMode == getModeID()) && (ModeLabel[thismode][i].label != NULL) && (ButtonLabel::ButtonVisible[bindex])){
			drawButtons(thismode);
		}


		return true;
	}
  }

  return false;
}


/*
  InputEvent::processKey(KeyID);
  Process keys normally brought in by hardware or keyboard presses
  Future will also allow for long and double click presses...
  Return = We had a valid key (even if nothing happens because of Bounce)
*/
bool InputEvents::processKey(int dWord) {
  if (!(ProgramStarted==psNormalOp)) return false;

  InterfaceTimeoutReset();

  int event_id;

  // Valid input ?
  if ((dWord < 0) || (dWord > MAX_KEY))
    return false;

  // get current mode
  int mode = InputEvents::getModeID();
  


  // Which key - can be defined locally or at default (fall back to default)
  event_id = Key2Event[mode][dWord];

// VENTA- DEBUG HARDWARE KEY PRESSED   
#ifdef VENTA_DEBUG_KEY
	TCHAR ventabuffer[80];
	wsprintf(ventabuffer,TEXT("PRCKEY %d MODE %d EVENT %d"), dWord, mode,event_id);
	DoStatusMessage(ventabuffer);
#endif
  if (event_id == 0) {
    // go with default key..
    event_id = Key2Event[0][dWord];
  }

  if (event_id > 0) {

    int bindex = -1;
    int lastMode = mode;
    const TCHAR *pLabelText = NULL;

    if (!Debounce()) return true;

	#ifndef DISABLEAUDIO
	// if (EnableSoundModes) PlayResource(TEXT("IDR_WAV_CLICK")); 
	#endif

    int i;
    for (i = ModeLabel_count[mode]; i >= 0; i--) {
      if ((ModeLabel[mode][i].event == event_id)) {
        bindex = ModeLabel[mode][i].location;
        pLabelText = ModeLabel[mode][i].label;
	#if 0 // REMOVE ANIMATION
        if (bindex>0) {
          ButtonLabel::AnimateButton(bindex);
        }
	#endif
      }
    }

    if (!ButtonLabel::ButtonDisabled[bindex]) {
      InputEvents::processGo(event_id);
    }

    // experimental: update button text, macro may change the value
    if ((lastMode == getModeID()) && (bindex > 0) && (pLabelText != NULL) && ButtonLabel::ButtonVisible[bindex]) {
      drawButtons(lastMode);
    }

    return true;
  }
  
  return false;
}


bool InputEvents::processNmea(int ne_id) {
  // add an event to the bottom of the queue
  LockEventQueue();
  for (int i=0; i< MAX_NMEA_QUEUE; i++) {
    if (NMEA_Queue[i]== -1) {
      NMEA_Queue[i]= ne_id;
      break;
    }
  }
  UnlockEventQueue();
  return true; // ok.
}

/*
  InputEvent::processNmea(TCHAR* data)
  Take hard coded inputs from NMEA processor.
  Return = TRUE if we have a valid key match
*/
bool InputEvents::processNmea_real(int ne_id) {
  if (!(ProgramStarted==psNormalOp)) return false;
  int event_id = 0;

  InterfaceTimeoutReset();

  // Valid input ?
  if ((ne_id < 0) || (ne_id >= NE_COUNT))
    return false;

  // get current mode
  int mode = InputEvents::getModeID();
  
  // Which key - can be defined locally or at default (fall back to default)
  event_id = N2Event[mode][ne_id];
  if (event_id == 0) {
    // go with default key..
    event_id = N2Event[0][ne_id];
  }

  if (event_id > 0) {
    InputEvents::processGo(event_id);
    return true;
  }
  
  return false;
}


// This should be called ONLY by the GUI thread.
void InputEvents::DoQueuedEvents(void) {
  static bool blockqueue = false;
  int GCE_Queue_copy[MAX_GCE_QUEUE];
  int NMEA_Queue_copy[MAX_NMEA_QUEUE];
  int i;

  if (blockqueue) return; 
  // prevent this being re-entered by gui thread while
  // still processing

  blockqueue = true;
  if (RepeatWindCalc>0) { // 100203
	RepeatWindCalc=0;
	InputEvents::eventCalcWind(_T("AUTO"));
	//dlgBasicSettingsShowModal();
  }

  // copy the queue first, blocking
  LockEventQueue();
  for (i=0; i<MAX_GCE_QUEUE; i++) {
    GCE_Queue_copy[i]= GCE_Queue[i];
  }
  for (i=0; i<MAX_NMEA_QUEUE; i++) {
    NMEA_Queue_copy[i]= NMEA_Queue[i];
  }
  UnlockEventQueue();

  // process each item in the queue
  for (i=0; i< MAX_GCE_QUEUE; i++) {
    if (GCE_Queue_copy[i]!= -1) {
      processGlideComputer_real(GCE_Queue_copy[i]);
    }
  }
  for (i=0; i< MAX_NMEA_QUEUE; i++) {
    if (NMEA_Queue_copy[i]!= -1) {
      processNmea_real(NMEA_Queue_copy[i]);
    }
  }

  // now flush the queue, again blocking
  LockEventQueue();
  for (i=0; i<MAX_GCE_QUEUE; i++) {
    GCE_Queue[i]= -1;
  }
  for (i=0; i<MAX_NMEA_QUEUE; i++) {
    NMEA_Queue[i]= -1;
  }
  UnlockEventQueue();

  blockqueue = false; // ok, ready to go on.

}


bool InputEvents::processGlideComputer(int gce_id) {
  // add an event to the bottom of the queue
  LockEventQueue();
  for (int i=0; i< MAX_GCE_QUEUE; i++) {
    if (GCE_Queue[i]== -1) {
      GCE_Queue[i]= gce_id;
      break;
    }
  }
  UnlockEventQueue();
  return true; // ok.
}

/*
  InputEvents::processGlideComputer
  Take virtual inputs from a Glide Computer to do special events
*/
bool InputEvents::processGlideComputer_real(int gce_id) {
  if (!(ProgramStarted==psNormalOp)) return false;
  int event_id = 0;

  // TODO feature: Log glide computer events to IGC file

  // Valid input ?
  if ((gce_id < 0) || (gce_id >= GCE_COUNT))
    return false;

  // get current mode
  int mode = InputEvents::getModeID();
  
  // Which key - can be defined locally or at default (fall back to default)
  event_id = GC2Event[mode][gce_id];
  if (event_id == 0) {
    // go with default key..
    event_id = GC2Event[0][gce_id];
  }

  if (event_id > 0) {
    InputEvents::processGo(event_id);
    return true;
  }
  
  return false;
}

extern int MenuTimeOut;

// EXECUTE an Event - lookup event handler and call back - no return
void InputEvents::processGo(int eventid) {
  if (!(ProgramStarted==psNormalOp)) return;

  // evnentid 0 is special for "noop" - otherwise check event
  // exists (pointer to function)
  if (eventid) {
    if (Events[eventid].event) {
      Events[eventid].event(Events[eventid].misc);
      MenuTimeOut = 0;
    }
    if (Events[eventid].next > 0)
      InputEvents::processGo(Events[eventid].next);
  }
  return;
}

// -----------------------------------------------------------------------
// Execution - list of things you can do
// -----------------------------------------------------------------------

// TODO code: Keep marker text for use in log file etc.
void InputEvents::eventMarkLocation(const TCHAR *misc) {
  if (_tcscmp(misc, TEXT("reset")) == 0) {
	#if USETOPOMARKS
	reset_marks = true;
	#endif
	for (short i=RESWP_FIRST_MARKER; i<=RESWP_LAST_MARKER; i++) {
        	WayPointList[i].Latitude=RESWP_INVALIDNUMBER;
        	WayPointList[i].Longitude=RESWP_INVALIDNUMBER;
        	WayPointList[i].Altitude=RESWP_INVALIDNUMBER;
	        WayPointList[i].Visible=FALSE;
	        WayPointList[i].FarVisible=FALSE;
	        WayPointCalc[i].WpType = WPT_UNKNOWN;
	}
  } else {
	#ifndef DISABLEAUDIO
	if (EnableSoundModes) LKSound(TEXT("DROPMARKER.WAV"));
	#endif

	LockFlightData();
	#if USETOPOMARKS
	MarkLocation(GPS_INFO.Longitude, GPS_INFO.Latitude);
	#else
	MarkLocation(GPS_INFO.Longitude, GPS_INFO.Latitude, CALCULATED_INFO.NavAltitude );
	#endif
	UnlockFlightData();
  }
}

void InputEvents::eventSounds(const TCHAR *misc) {
  
  if (_tcscmp(misc, TEXT("toggle")) == 0)
    EnableSoundModes = !EnableSoundModes;
  else if (_tcscmp(misc, TEXT("on")) == 0)
    EnableSoundModes = true;
  else if (_tcscmp(misc, TEXT("off")) == 0)
    EnableSoundModes = false;
  else if (_tcscmp(misc, TEXT("show")) == 0) {
    if (EnableSoundModes)
	// LKTOKEN  _@M625_ = "Sounds ON" 
      DoStatusMessage(gettext(TEXT("_@M625_")));
    else
	// LKTOKEN  _@M624_ = "Sounds OFF" 
      DoStatusMessage(gettext(TEXT("_@M624_")));  
  }
}

void InputEvents::eventBaroAltitude(const TCHAR *misc) {
  
  if (_tcscmp(misc, TEXT("toggle")) == 0)
    EnableNavBaroAltitude = !EnableNavBaroAltitude;
  else if (_tcscmp(misc, TEXT("on")) == 0)
    EnableNavBaroAltitude = true;
  else if (_tcscmp(misc, TEXT("off")) == 0)
    EnableNavBaroAltitude = false;
  else if (_tcscmp(misc, TEXT("show")) == 0) {
    if (EnableNavBaroAltitude)
	// LKTOKEN  _@M756_ = "USING BARO ALTITUDE" 
      DoStatusMessage(gettext(TEXT("_@M756_")));
    else
	// LKTOKEN  _@M757_ = "USING GPS ALTITUDE" 
      DoStatusMessage(gettext(TEXT("_@M757_")));  
  }
}

void InputEvents::eventSnailTrail(const TCHAR *misc) {

  if (_tcscmp(misc, TEXT("toggle")) == 0) {
    TrailActive ++;
    if (TrailActive>3) {
      TrailActive=0;
    }
  } 
  else if (_tcscmp(misc, TEXT("off")) == 0)
    TrailActive = 0;
  else if (_tcscmp(misc, TEXT("long")) == 0)
    TrailActive = 1;
  else if (_tcscmp(misc, TEXT("short")) == 0)
    TrailActive = 2;
  else if (_tcscmp(misc, TEXT("full")) == 0)
    TrailActive = 3;

  else if (_tcscmp(misc, TEXT("show")) == 0) {
    if (TrailActive==0)
	// LKTOKEN  _@M620_ = "SnailTrail OFF" 
      DoStatusMessage(gettext(TEXT("_@M620_")));
    if (TrailActive==1) 
	// LKTOKEN  _@M622_ = "SnailTrail ON Long" 
      DoStatusMessage(gettext(TEXT("_@M622_")));
    if (TrailActive==2) 
	// LKTOKEN  _@M623_ = "SnailTrail ON Short" 
      DoStatusMessage(gettext(TEXT("_@M623_")));
    if (TrailActive==3) 
	// LKTOKEN  _@M621_ = "SnailTrail ON Full" 
      DoStatusMessage(gettext(TEXT("_@M621_")));
  }  
}

void InputEvents::eventVisualGlide(const TCHAR *misc) {

  if (_tcscmp(misc, TEXT("toggle")) == 0) {
    VisualGlide ++;
    if (VisualGlide==2 && !ExtendedVisualGlide) VisualGlide=0;
    if (VisualGlide>2) {
      VisualGlide=0;
    }
  } 
  else if (_tcscmp(misc, TEXT("off")) == 0)
    VisualGlide = 0;
  else if (_tcscmp(misc, TEXT("steady")) == 0)
    VisualGlide = 1;
  else if (_tcscmp(misc, TEXT("moving")) == 0)
    VisualGlide = 2;

  else if (_tcscmp(misc, TEXT("show")) == 0) {
    if (VisualGlide==0)
      DoStatusMessage(TEXT("VisualGlide OFF"));
    if (VisualGlide==1) 
      DoStatusMessage(TEXT("VisualGlide Steady"));
    if (VisualGlide==2) 
      DoStatusMessage(TEXT("VisualGlide Moving"));
  }  
}

void InputEvents::eventAirSpace(const TCHAR *misc) {
  if (_tcscmp(misc, TEXT("toggle")) == 0) {
    OnAirSpace ++;
    if (OnAirSpace>1) {
      OnAirSpace=0;
    }
  } 
  else if (_tcscmp(misc, TEXT("off")) == 0)
    OnAirSpace = 0;
  else if (_tcscmp(misc, TEXT("on")) == 0)
    OnAirSpace = 1;
  else if (_tcscmp(misc, TEXT("show")) == 0) {
    if (OnAirSpace==0)
	// LKTOKEN  _@M613_ = "Show AirSpace OFF" 
      DoStatusMessage(gettext(TEXT("_@M613_")));
    if (OnAirSpace==1) 
	// LKTOKEN  _@M614_ = "Show AirSpace ON" 
      DoStatusMessage(gettext(TEXT("_@M614_")));
  }  
}

void InputEvents::eventActiveMap(const TCHAR *misc) {
  if (_tcscmp(misc, TEXT("toggle")) == 0) {
	if (ActiveMap)
		ActiveMap=false;
	else
		ActiveMap=true;
  } 
  else if (_tcscmp(misc, TEXT("off")) == 0)
    ActiveMap=false;
  else if (_tcscmp(misc, TEXT("on")) == 0)
    ActiveMap=true;
  else if (_tcscmp(misc, TEXT("show")) == 0) {
    if (ActiveMap)
	// 854 ActiveMap ON
      DoStatusMessage(gettext(TEXT("_@M854_")));
    if (!ActiveMap) 
	// 855 ActiveMap OFF
      DoStatusMessage(gettext(TEXT("_@M855_")));
  }  
}


void InputEvents::eventScreenModes(const TCHAR *misc) {

}



// eventAutoZoom - Turn on|off|toggle AutoZoom
// misc:
//	auto on - Turn on if not already
//	auto off - Turn off if not already
//	auto toggle - Toggle current full screen status
//	auto show - Shows autozoom status
//	+	- Zoom in
//	++	- Zoom in near
//	-	- Zoom out
//	--	- Zoom out far
//	n.n	- Zoom to a set scale
//	show - Show current zoom scale
void InputEvents::eventZoom(const TCHAR* misc) {
  // JMW pass through to handler in MapWindow
  // here:
  // -1 means toggle
  // 0 means off
  // 1 means on
  float zoom;

  if (_tcscmp(misc, TEXT("auto toggle")) == 0)
    MapWindow::zoom.EventAutoZoom(-1);
  else if (_tcscmp(misc, TEXT("auto on")) == 0)
    MapWindow::zoom.EventAutoZoom(1);
  else if (_tcscmp(misc, TEXT("auto off")) == 0)
    MapWindow::zoom.EventAutoZoom(0);
  else if (_tcscmp(misc, TEXT("auto show")) == 0) {
    if (MapWindow::zoom.AutoZoom())
	// 856 AutoZoom ON
      DoStatusMessage(gettext(TEXT("_@M856_")));
    else
	// 857 AutoZoom OFF
      DoStatusMessage(gettext(TEXT("_@M857_")));
  }
  else if (_tcscmp(misc, TEXT("slowout")) == 0)
    MapWindow::zoom.EventScaleZoom(-4);
  else if (_tcscmp(misc, TEXT("slowin")) == 0)
    MapWindow::zoom.EventScaleZoom(4);
  else if (_tcscmp(misc, TEXT("out")) == 0)
    MapWindow::zoom.EventScaleZoom(-1);
  else if (_tcscmp(misc, TEXT("in")) == 0)
    MapWindow::zoom.EventScaleZoom(1);
  else if (_tcscmp(misc, TEXT("-")) == 0)
    MapWindow::zoom.EventScaleZoom(-1);
  else if (_tcscmp(misc, TEXT("+")) == 0)
    MapWindow::zoom.EventScaleZoom(1);
  else if (_tcscmp(misc, TEXT("--")) == 0)
    MapWindow::zoom.EventScaleZoom(-2);
  else if (_tcscmp(misc, TEXT("++")) == 0) 
    MapWindow::zoom.EventScaleZoom(2);
  else if (_stscanf(misc, TEXT("%f"), &zoom) == 1)
    MapWindow::zoom.EventSetZoom((double)zoom);

  else if (_tcscmp(misc, TEXT("circlezoom toggle")) == 0) {
    MapWindow::zoom.CircleZoom(!MapWindow::zoom.CircleZoom());
    MapWindow::zoom.SwitchMode();
  } else if (_tcscmp(misc, TEXT("circlezoom on")) == 0) {
    MapWindow::zoom.CircleZoom(true);
    MapWindow::zoom.SwitchMode();
  } else if (_tcscmp(misc, TEXT("circlezoom off")) == 0) {
    MapWindow::zoom.CircleZoom(false);
    MapWindow::zoom.SwitchMode();
  } else if (_tcscmp(misc, TEXT("circlezoom show")) == 0) {
    if (MapWindow::zoom.CircleZoom())
	// LKTOKEN  _@M173_ = "Circling Zoom ON" 
      DoStatusMessage(gettext(TEXT("_@M173_")));
    else
	// LKTOKEN  _@M172_ = "Circling Zoom OFF" 
      DoStatusMessage(gettext(TEXT("_@M172_")));
  }

}

// Pan
//	on	Turn pan on
//	off	Turn pan off
//      supertoggle Toggles pan and fullscreen
//	up	Pan up
//	down	Pan down
//	left	Pan left
//	right	Pan right
void InputEvents::eventPan(const TCHAR *misc) {
  if (_tcscmp(misc, TEXT("toggle")) == 0)
    MapWindow::Event_Pan(-1);
  else if (_tcscmp(misc, TEXT("supertoggle")) == 0)
    MapWindow::Event_Pan(-2);
  else if (_tcscmp(misc, TEXT("on")) == 0) 
    MapWindow::Event_Pan(1);
  else if (_tcscmp(misc, TEXT("off")) == 0) 
    MapWindow::Event_Pan(0);

 else if (_tcscmp(misc, TEXT("up")) == 0)
			MapWindow::zoom.EventScaleZoom(1);
else if (_tcscmp(misc, TEXT("down")) == 0)
			MapWindow::zoom.EventScaleZoom(-1); // fixed v58
  else if (_tcscmp(misc, TEXT("left")) == 0)
    MapWindow::Event_PanCursor(1,0);
  else if (_tcscmp(misc, TEXT("right")) == 0)
    MapWindow::Event_PanCursor(-1,0);
  else if (_tcscmp(misc, TEXT("show")) == 0) {
    if (MapWindow::mode.AnyPan())
      DoStatusMessage(gettext(TEXT("_@M858_"))); // Pan mode ON
    else
      DoStatusMessage(gettext(TEXT("_@M859_"))); // Pan mode OFF
  }

}

// Do JUST Terrain/Toplogy (toggle any, on/off any, show)
void InputEvents::eventTerrainTopology(const TCHAR *misc) {

  if (_tcscmp(misc, TEXT("terrain toggle")) == 0) 
    MapWindow::Event_TerrainTopology(-2);

  else if (_tcscmp(misc, TEXT("topology toggle")) == 0) 
    MapWindow::Event_TerrainTopology(-3);

  else if (_tcscmp(misc, TEXT("terrain on")) == 0) 
    MapWindow::Event_TerrainTopology(3);

  else if (_tcscmp(misc, TEXT("terrain off")) == 0) 
    MapWindow::Event_TerrainTopology(4);

  else if (_tcscmp(misc, TEXT("topology on")) == 0) 
    MapWindow::Event_TerrainTopology(1);

  else if (_tcscmp(misc, TEXT("topology off")) == 0) 
    MapWindow::Event_TerrainTopology(2);

  else if (_tcscmp(misc, TEXT("show")) == 0) 
    MapWindow::Event_TerrainTopology(0);

  else if (_tcscmp(misc, TEXT("toggle")) == 0) 
    MapWindow::Event_TerrainTopology(-1);

}

#if 0  // REMOVE
// Do clear warnings IF NONE Toggle Terrain/Topology
void InputEvents::eventClearWarningsOrTerrainTopology(const TCHAR *misc) {
	(void)misc;
  #if USEOLDASPWARNINGS
  if (0) {
	// airspace was active, enter was used to acknowledge
    return;
  }
  #endif
  // Else toggle TerrainTopology - and show the results
  MapWindow::Event_TerrainTopology(-1);
  MapWindow::Event_TerrainTopology(0);
}
#endif

#if USEOLDASPWARNINGS
// ClearAirspaceWarnings
// Clears airspace warnings for the selected airspace
//     day: clears the warnings for the entire day
//     ack: clears the warnings for the acknowledgement time
void InputEvents::eventClearAirspaceWarnings(const TCHAR *misc) {
  // this is not used anymore.
}
#endif

#if 0 // REMOVE
// ClearStatusMessages
// Do Clear Event Warnings
void InputEvents::eventClearStatusMessages(const TCHAR *misc) {
	(void)misc;
  ClearStatusMessages();
  // TODO enhancement: allow selection of specific messages (here we are acknowledging all)
  Message::Acknowledge(0);
}
#endif


// ArmAdvance
// Controls waypoint advance trigger:
//     on: Arms the advance trigger
//    off: Disarms the advance trigger
//   toggle: Toggles between armed and disarmed.
//   show: Shows current armed state
void InputEvents::eventArmAdvance(const TCHAR *misc) {
  if (AutoAdvance>=2) {
    if (_tcscmp(misc, TEXT("on")) == 0) {
      AdvanceArmed = true;
    }
    if (_tcscmp(misc, TEXT("off")) == 0) {
      AdvanceArmed = false;
    }
    if (_tcscmp(misc, TEXT("toggle")) == 0) {
      AdvanceArmed = !AdvanceArmed;
    }
  }
  if (_tcscmp(misc, TEXT("show")) == 0) {
    switch (AutoAdvance) {
    case 0:
	// LKTOKEN  _@M105_ = "Auto Advance: Manual" 
      DoStatusMessage(gettext(TEXT("_@M105_")));
      break;
    case 1:
	// LKTOKEN  _@M103_ = "Auto Advance: Automatic" 
      DoStatusMessage(gettext(TEXT("_@M103_")));
      break;
    case 2:
      if (AdvanceArmed) {
	// LKTOKEN  _@M102_ = "Auto Advance: ARMED" 
        DoStatusMessage(gettext(TEXT("_@M102_")));
      } else {
	// LKTOKEN  _@M104_ = "Auto Advance: DISARMED" 
        DoStatusMessage(gettext(TEXT("_@M104_")));
      }
      break;
    case 3:
      if (ActiveWayPoint<2) { // past start (but can re-start)
        if (AdvanceArmed) {
	// LKTOKEN  _@M102_ = "Auto Advance: ARMED" 
          DoStatusMessage(gettext(TEXT("_@M102_")));
        } else {
	// LKTOKEN  _@M104_ = "Auto Advance: DISARMED" 
          DoStatusMessage(gettext(TEXT("_@M104_")));
        }
      } else {
	// LKTOKEN  _@M103_ = "Auto Advance: Automatic" 
        DoStatusMessage(gettext(TEXT("_@M103_")));
      }
      break;    
    default:
      break;
    }
  }
}

// Mode
// Sets the current event mode.
//  The argument is the label of the mode to activate. 
//  This is used to activate menus/submenus of buttons
void InputEvents::eventMode(const TCHAR *misc) {
  ASSERT(misc != NULL);
  InputEvents::setMode(misc);
  
  // trigger redraw of screen to reduce blank area under windows
  MapWindow::RequestFastRefresh();
}


// Checklist
// Displays the checklist dialog
//  See the checklist dialog section of the reference manual for more info.
void InputEvents::eventChecklist(const TCHAR *misc) {
	(void)misc;
  dlgChecklistShowModal();
}

#if 0 // REMOVE, unused since xci 4
// FLARM Traffic
// Displays the FLARM traffic dialog
// See the checklist dialog section of the reference manual for more info.
void InputEvents::eventFlarmTraffic(const TCHAR *misc) {
	(void)misc;
#if 0
  dlgFlarmTrafficShowModal();
#endif
}
#endif
 

// Displays the task calculator dialog
//  See the task calculator dialog section of the reference manual
// for more info.
void InputEvents::eventCalculator(const TCHAR *misc) {
	(void)misc;
  dlgTaskCalculatorShowModal();
}

// Status
// Displays one of the three status dialogs:
//    system: display the system status
//    aircraft: displays the aircraft status
//    task: displays the task status
//  See the status dialog section of the reference manual for more info
//  on these.
void InputEvents::eventStatus(const TCHAR *misc) {
  if (_tcscmp(misc, TEXT("system")) == 0) {
    dlgStatusShowModal(1);
  } else if (_tcscmp(misc, TEXT("task")) == 0) {
    dlgStatusShowModal(2);
  } else if (_tcscmp(misc, TEXT("Aircraft")) == 0) {
    dlgStatusShowModal(0);
  } else {
    dlgStatusShowModal(-1);
  }
}

// Analysis
// Displays the analysis/statistics dialog
//  See the analysis dialog section of the reference manual
// for more info.
void InputEvents::eventAnalysis(const TCHAR *misc) {
	(void)misc;
  PopupAnalysis();
}

// WaypointDetails
// Displays waypoint details
//         current: the current active waypoint
//          select: brings up the waypoint selector, if the user then
//                  selects a waypoint, then the details dialog is shown.
//  See the waypoint dialog section of the reference manual
// for more info.
void InputEvents::eventWaypointDetails(const TCHAR *misc) {

  if (_tcscmp(misc, TEXT("current")) == 0) {
    if (ValidTaskPoint(ActiveWayPoint)) { // BUGFIX 091116
      SelectedWaypoint = Task[ActiveWayPoint].Index;
    }
    if (SelectedWaypoint<0){
	// LKTOKEN  _@M462_ = "No Active Waypoint!" 
      DoStatusMessage(gettext(TEXT("_@M462_")));
      return;
    }
    PopupWaypointDetails();
  } else
    if (_tcscmp(misc, TEXT("select")) == 0) {
      int res = dlgWayPointSelect();

      if (res != -1){
	SelectedWaypoint = res;
	PopupWaypointDetails();
      };

    }
}

void InputEvents::eventTimeGates(const TCHAR *misc) {
	dlgTimeGatesShowModal();
}

void InputEvents::eventGotoLookup(const TCHAR *misc) {
  int res = dlgWayPointSelect();
  if (res != -1){
    FlyDirectTo(res);
  };
}


// StatusMessage
// Displays a user defined status message. 
//    The argument is the text to be displayed.
//    No punctuation characters are allowed.
void InputEvents::eventStatusMessage(const TCHAR *misc) {
  // 110102 shorthack to handle lktokens and any character
  if (_tcslen(misc)>4) {
	if (misc[0]=='Z' && misc[1]=='Y' && misc[2]=='X') {	// ZYX synthetic tokens inside XCIs
		TCHAR nmisc[10];
		_tcscpy(nmisc,_T("_@M"));
		_tcscat(nmisc,&misc[3]);
		_tcscat(nmisc,_T("_"));
		DoStatusMessage(LKGetText(nmisc));
		return;
	}
  } 
  DoStatusMessage(misc);
}

// Plays a sound from the filename
void InputEvents::eventPlaySound(const TCHAR *misc) {
  PlayResource(misc);
}

// MacCready
// Adjusts MacCready settings
// up, down, auto on, auto off, auto toggle, auto show
void InputEvents::eventMacCready(const TCHAR *misc) {
  if (_tcscmp(misc, TEXT("up")) == 0) {
    MacCreadyProcessing(1);
  } else if (_tcscmp(misc, TEXT("down")) == 0) {
    MacCreadyProcessing(-1);
  } else if (_tcscmp(misc, TEXT("5up")) == 0) {
    MacCreadyProcessing(+3);
  } else if (_tcscmp(misc, TEXT("5down")) == 0) {
    MacCreadyProcessing(-3);
  } else if (_tcscmp(misc, TEXT("auto toggle")) == 0) {
    MacCreadyProcessing(0);
  } else if (_tcscmp(misc, TEXT("auto on")) == 0) {
    MacCreadyProcessing(+2);
  } else if (_tcscmp(misc, TEXT("auto off")) == 0) {
    MacCreadyProcessing(-2);
  } else if (_tcscmp(misc, TEXT("auto final")) == 0) {
    MacCreadyProcessing(-5);
  } else if (_tcscmp(misc, TEXT("auto climb")) == 0) {
    MacCreadyProcessing(-6);
  } else if (_tcscmp(misc, TEXT("auto equiv")) == 0) {
    MacCreadyProcessing(-7);
  } else if (_tcscmp(misc, TEXT("auto show")) == 0) {
    if (CALCULATED_INFO.AutoMacCready) {
      DoStatusMessage(gettext(TEXT("_@M860_"))); // Auto MacCready ON
    } else {
      DoStatusMessage(gettext(TEXT("_@M861_"))); // Auto MacCready OFF
    }
  } else if (_tcscmp(misc, TEXT("show")) == 0) {
    TCHAR Temp[100];
    _stprintf(Temp,TEXT("%0.1f"),MACCREADY*LIFTMODIFY);
    DoStatusMessage(TEXT("MacCready "), Temp);
  }
}

void InputEvents::eventChangeWindCalcSpeed(const TCHAR *misc) {
  if (_tcscmp(misc, TEXT("5up")) == 0) {
	ChangeWindCalcSpeed(5);
  } else if (_tcscmp(misc, TEXT("10up")) == 0) {
	ChangeWindCalcSpeed(10);
  } else if (_tcscmp(misc, TEXT("5down")) == 0) {
	ChangeWindCalcSpeed(-5);
  } else if (_tcscmp(misc, TEXT("10down")) == 0) {
	ChangeWindCalcSpeed(-10);
  }
}

void InputEvents::eventChangeMultitarget(const TCHAR *misc) {
  if (_tcscmp(misc, TEXT("TASK")) == 0) {
	OvertargetMode=OVT_TASK;
  } else if (_tcscmp(misc, TEXT("BALT")) == 0) {
	OvertargetMode=OVT_BALT;
  } else if (_tcscmp(misc, TEXT("ALT1")) == 0) {
	OvertargetMode=OVT_ALT1;
  } else if (_tcscmp(misc, TEXT("ALT2")) == 0) {
	OvertargetMode=OVT_ALT2;
  } else if (_tcscmp(misc, TEXT("HOME")) == 0) {
	OvertargetMode=OVT_HOME;
  } else if (_tcscmp(misc, TEXT("THER")) == 0) {
	OvertargetMode=OVT_THER;
  } else if (_tcscmp(misc, TEXT("MATE")) == 0) {
	OvertargetMode=OVT_MATE;
  } else if (_tcscmp(misc, TEXT("FLARM")) == 0) {
	OvertargetMode=OVT_FLARM;
  } else if (_tcscmp(misc, TEXT("MARK")) == 0) {
	OvertargetMode=OVT_MARK;
  } else if (_tcscmp(misc, TEXT("PASS")) == 0) {
	OvertargetMode=OVT_PASS;
  }
}


// Allows forcing of flight mode (final glide)
void InputEvents::eventFlightMode(const TCHAR *misc) {
  if (_tcscmp(misc, TEXT("finalglide on")) == 0) {
    ForceFinalGlide = true;
  }
  if (_tcscmp(misc, TEXT("finalglide off")) == 0) {
    ForceFinalGlide = false;
  }
  if (_tcscmp(misc, TEXT("finalglide toggle")) == 0) {
    ForceFinalGlide = !ForceFinalGlide;
  }
  if (_tcscmp(misc, TEXT("show")) == 0) {
    if (ForceFinalGlide) {
	// LKTOKEN  _@M289_ = "Final glide forced ON" 
      DoStatusMessage(gettext(TEXT("_@M289_")));
    } else {
	// LKTOKEN  _@M288_ = "Final glide automatic" 
      DoStatusMessage(gettext(TEXT("_@M288_")));
    }
  }
  if (ForceFinalGlide && ActiveWayPoint == -1){
	// LKTOKEN  _@M462_ = "No Active Waypoint!" 
    DoStatusMessage(gettext(TEXT("_@M462_")));
  }
}



// Wind
// Adjusts the wind magnitude and direction
//     up: increases wind magnitude
//   down: decreases wind magnitude
//   left: rotates wind direction counterclockwise
//  right: rotates wind direction clockwise
//   save: saves wind value, so it is used at next startup
//
// TODO feature: Increase wind by larger amounts ? Set wind to specific amount ?
//	(may sound silly - but future may get SMS event that then sets wind)
void InputEvents::eventWind(const TCHAR *misc) {
  if (_tcscmp(misc, TEXT("up")) == 0) {
    WindSpeedProcessing(1);
  }
  if (_tcscmp(misc, TEXT("down")) == 0) {
    WindSpeedProcessing(-1);
  }
  if (_tcscmp(misc, TEXT("left")) == 0) {
    WindSpeedProcessing(-2);
  }
  if (_tcscmp(misc, TEXT("right")) == 0) {
    WindSpeedProcessing(2);
  }
  if (_tcscmp(misc, TEXT("save")) == 0) {
    WindSpeedProcessing(0);
  }
}


// SendNMEA
//  Sends a user-defined NMEA string to an external instrument.
//   The string sent is prefixed with the start character '$'
//   and appended with the checksum e.g. '*40'.  The user needs only
//   to provide the text in between the '$' and '*'.
//
void InputEvents::eventSendNMEA(const TCHAR *misc) {
  if (misc) {
    // Currently disabled, because nonexistent function
    // PortWriteNMEA(misc);
  }
}

void InputEvents::eventSendNMEAPort1(const TCHAR *misc) {
  if (misc) {
    Port1WriteNMEA(misc);
  }
}

void InputEvents::eventSendNMEAPort2(const TCHAR *misc) {
  if (misc) {
    Port2WriteNMEA(misc);
  }
}


// AdjustWaypoint
// Adjusts the active waypoint of the task  
//  next: selects the next waypoint, stops at final waypoint
//  previous: selects the previous waypoint, stops at start waypoint
//  nextwrap: selects the next waypoint, wrapping back to start after final
//  previouswrap: selects the previous waypoint, wrapping to final after start
void InputEvents::eventAdjustWaypoint(const TCHAR *misc) {
  if (_tcscmp(misc, TEXT("next")) == 0) {
    NextUpDown(1); // next
  } else if (_tcscmp(misc, TEXT("nextwrap")) == 0) {
    NextUpDown(2); // next - with wrap
  } else if (_tcscmp(misc, TEXT("previous")) == 0) {
    NextUpDown(-1); // previous
  } else if (_tcscmp(misc, TEXT("previouswrap")) == 0) {
    NextUpDown(-2); // previous with wrap
  }
}


// There is no more Suspend task 091216, eventAbortTask changed
void InputEvents::eventAbortTask(const TCHAR *misc) {

  if (ValidTaskPoint(ActiveWayPoint)) {
	if (MessageBoxX(hWndMapWindow, 
	// LKTOKEN  _@M179_ = "Clear the task?" 
		gettext(TEXT("_@M179_")), 
	// LKTOKEN  _@M178_ = "Clear task" 
		gettext(TEXT("_@M178_")), 
		MB_YESNO|MB_ICONQUESTION) == IDYES) {
		// clear task is locking taskdata already
		ClearTask();
	}
  } else {
	MessageBoxX(hWndMapWindow, 
	// LKTOKEN  _@M468_ = "No Task" 
		gettext(TEXT("_@M468_")), 
	// LKTOKEN  _@M178_ = "Clear task" 
		gettext(TEXT("_@M178_")), 
		MB_OK|MB_ICONEXCLAMATION);
  }
}

#define RESCHEDTIME 20
//  if AUTO mode, be quiet and say nothing until successful
void InputEvents::eventCalcWind(const TCHAR *misc) {

  double wfrom=0, wspeed=0;
  int resw=0;
  static TCHAR mbuf[200];
  TCHAR ttmp[50];
  bool reschedule=false, automode=false;
  static int wmode=0;
  // number of seconds to retry after a failure to calculate
  static short retry=RESCHEDTIME;


  // TODO use digital compass if available, using D as degrees example D125
  if (misc) {
	if (_tcscmp(misc,_T("C0")) == 0) {
		wmode=0;
	} 
	if (_tcscmp(misc,_T("C1")) == 0) {
		wmode=1;
	}
	if (_tcscmp(misc,_T("C2")) == 0) {
		wmode=2;
	}
  } 

  // if not automode, prepare for next automode retries
  if (misc && _tcscmp(misc,_T("AUTO"))==0) {
	automode=true;
  } else {
	RepeatWindCalc=0;
	retry=RESCHEDTIME;
  }
  

  // IAS if available is used automatically and in this case WindCalcSpeed is ignored
  resw=CalculateWindRotary(&rotaryWind,WindCalcSpeed, &wfrom, &wspeed,WindCalcTime, wmode);

  if (resw<=0) {
	switch(resw) {
		case WCALC_INVALID_SPEED:
	// LKTOKEN  _@M369_ = "KEEP SPEED LONGER PLEASE" 
			_stprintf(mbuf,gettext(TEXT("_@M369_")));
			reschedule=true;
			break;
		case WCALC_INVALID_TRACK:
	// LKTOKEN  _@M367_ = "KEEP HEADING LONGER PLEASE" 
			_stprintf(mbuf,gettext(TEXT("_@M367_")));
			reschedule=true;
			break;
		case WCALC_INVALID_ALL:
	// LKTOKEN  _@M368_ = "KEEP SPEED AND HEADING LONGER PLEASE" 
			_stprintf(mbuf,gettext(TEXT("_@M368_")));
			reschedule=true;
			break;
		case WCALC_INVALID_HEADING:
	// LKTOKEN  _@M344_ = "INACCURATE HEADING OR TOO STRONG WIND" 
			_stprintf(mbuf,gettext(TEXT("_@M344_")));
			break;
		case WCALC_INVALID_IAS:
	// LKTOKEN  _@M366_ = "KEEP AIRSPEED CONSTANT LONGER PLEASE" 
			_stprintf(mbuf,gettext(TEXT("_@M366_")));
			reschedule=true;
			break;
		case WCALC_INVALID_NOIAS:
	// LKTOKEN  _@M345_ = "INVALID AIRSPEED" 
			_stprintf(mbuf,gettext(TEXT("_@M345_")));
			break;
		default:
			_stprintf(mbuf,_T("INVALID DATA CALCULATING WIND %d"), resw); 
			break;
	}
	if (automode==true) {
		if (reschedule && --retry>0) 
			RepeatWindCalc=1;
		else
			// be sure that we dont enter a loop here!
			RepeatWindCalc=0;
	} else {
		if (reschedule) {
			DoStatusMessage(mbuf);
			RepeatWindCalc=1;
		} else {
			DoStatusMessage(mbuf);
			RepeatWindCalc=0;
		}
	}
	return;
  }

  _stprintf(mbuf,_T("%.0f%s from %.0f")TEXT(DEG)_T("\n\nAccept and save?"), 
	wspeed/3.6*SPEEDMODIFY, Units::GetHorizontalSpeedName(), wfrom);

#if 0
  if (reswp<80) _stprintf(ttmp,_T("TrueWind! Quality: low"));
  if (reswp<98) _stprintf(ttmp,_T("TrueWind! Quality: fair"));
  if (reswp>=98) _stprintf(ttmp,_T("TrueWind! Quality: good"));
#else
  _stprintf(ttmp,_T("TrueWind! %s: %d%%"),gettext(TEXT("_@M866_")),resw); // Quality
#endif

  if (MessageBoxX(hWndMapWindow, mbuf, ttmp, MB_YESNO|MB_ICONQUESTION) == IDYES) {

	SetWindEstimate(wspeed/3.6,wfrom);

	CALCULATED_INFO.WindSpeed=wspeed/3.6;
	CALCULATED_INFO.WindBearing=wfrom;

	// LKTOKEN  _@M746_ = "TrueWind updated!" 
	DoStatusMessage(gettext(TEXT("_@M746_")));
  }
}

void InputEvents::eventInvertColor(const TCHAR *misc) { // 100114
  static bool doinit=true;
  static short oldOutline;
  if (doinit) {
	oldOutline=OutlinedTp;
	doinit=false;
  }

  if (OutlinedTp>(OutlinedTp_t)otDisabled)
	OutlinedTp=(OutlinedTp_t)otDisabled;
  else
	OutlinedTp=oldOutline;
  Appearance.InverseInfoBox = !Appearance.InverseInfoBox;
  return;

}

void InputEvents::eventResetTask(const TCHAR *misc) { // 100117

  if (ValidTaskPoint(ActiveWayPoint) && ValidTaskPoint(1)) {
	if (MessageBoxX(hWndMapWindow, 
	// LKTOKEN  _@M563_ = "Restart task?" 
		gettext(TEXT("_@M563_")), 
	// LKTOKEN  _@M562_ = "Restart task" 
		gettext(TEXT("_@M562_")), 
		MB_YESNO|MB_ICONQUESTION) == IDYES) {
		LockTaskData();
		ResetTask();
		UnlockTaskData();
	}
  } else {
	MessageBoxX(hWndMapWindow, 
	// LKTOKEN  _@M468_ = "No Task" 
		gettext(TEXT("_@M468_")), 
	// LKTOKEN  _@M562_ = "Restart task" 
		gettext(TEXT("_@M562_")), 
		MB_OK|MB_ICONEXCLAMATION);
  }

}

void InputEvents::eventResetQFE(const TCHAR *misc) { // 100211
	if (MessageBoxX(hWndMapWindow, 
	// LKTOKEN  _@M557_ = "Reset QFE?" 
		gettext(TEXT("_@M557_")), 
	// LKTOKEN  _@M559_ = "Reset zero QFE" 
		gettext(TEXT("_@M559_")), 
		MB_YESNO|MB_ICONQUESTION) == IDYES) {
			QFEAltitudeOffset=ALTITUDEMODIFY*CALCULATED_INFO.NavAltitude; // 100211
	}

}

void InputEvents::eventRestartCommPorts(const TCHAR *misc) { // 100211
	if (MessageBoxX(hWndMapWindow, 
	// LKTOKEN  _@M558_ = "Reset and restart COM Ports?" 
		gettext(TEXT("_@M558_")), 
	// LKTOKEN  _@M538_ = "RESET ALL COMM PORTS" 
		gettext(TEXT("_@M538_")), 
		MB_YESNO|MB_ICONQUESTION) == IDYES) {
			LKForceComPortReset=true;
			// also reset warnings
			PortMonitorMessages=0; // 100221
	}
}

// Simple events with no arguments. 
// USE SERVICE EVENTS INSTEAD OF CREATING NEW EVENTS!  
void InputEvents::eventService(const TCHAR *misc) { 

  if (_tcscmp(misc, TEXT("TASKSTART")) == 0) {
	TCHAR TempTime[40];
	TCHAR TempAlt[40];
	TCHAR TempSpeed[40];
	Units::TimeToText(TempTime, (int)TimeLocal((int)CALCULATED_INFO.TaskStartTime));
	_stprintf(TempAlt, TEXT("%.0f %s"), CALCULATED_INFO.TaskStartAltitude*ALTITUDEMODIFY, Units::GetAltitudeName());
	_stprintf(TempSpeed, TEXT("%.0f %s"), CALCULATED_INFO.TaskStartSpeed*TASKSPEEDMODIFY, Units::GetTaskSpeedName());

	TCHAR TempAll[120];
	_stprintf(TempAll, TEXT("\r\n%s: %s\r\n%s:%s\r\n%s: %s"),
		// Altitude
		gettext(TEXT("_@M89_")),
		TempAlt,
		// Speed
		gettext(TEXT("_@M632_")),
		TempSpeed,
		// Time
		gettext(TEXT("_@M720_")),
		TempTime);


	// ALWAYS issue DoStatusMessage BEFORE sounds, if possible.
	// LKTOKEN  _@M692_ = "Task Start" 
	DoStatusMessage(gettext(TEXT("_@M692_")), TempAll);
        if (EnableSoundModes) {
                LKSound(_T("LK_TASKSTART.WAV"));
        }
	return;
  }

  if (_tcscmp(misc, TEXT("TASKFINISH")) == 0) {
	TCHAR TempTime[40];
	TCHAR TempAlt[40];
	TCHAR TempSpeed[40];
	TCHAR TempTskSpeed[40];
	Units::TimeToText(TempTime, (int)TimeLocal((int)GPS_INFO.Time));
	_stprintf(TempAlt, TEXT("%.0f %s"), CALCULATED_INFO.NavAltitude*ALTITUDEMODIFY, Units::GetAltitudeName());
	_stprintf(TempSpeed, TEXT("%.0f %s"), GPS_INFO.Speed*TASKSPEEDMODIFY, Units::GetTaskSpeedName());
	_stprintf(TempTskSpeed, TEXT("%.2f %s"), CALCULATED_INFO.TaskSpeedAchieved*TASKSPEEDMODIFY, Units::GetTaskSpeedName());

	TCHAR TempAll[180];

	_stprintf(TempAll, TEXT("\r\n%s: %s\r\n%s:%s\r\n%s: %s\r\n%s: %s"),
	// Altitude
	gettext(TEXT("_@M89_")),
	TempAlt, 
	// Speed
	gettext(TEXT("_@M632_")),
	TempSpeed, 
	// Time
	gettext(TEXT("_@M720_")),
	TempTime, 
	// task speed
	gettext(TEXT("_@M697_")),
	TempTskSpeed);

	// LKTOKEN  _@M687_ = "Task Finish" 
	DoStatusMessage(gettext(TEXT("_@M687_")), TempAll);
	if (EnableSoundModes) {
		LKSound(_T("LK_TASKFINISH.WAV"));
	}
	return;
  }

  if (_tcscmp(misc, TEXT("TASKNEXTWAYPOINT")) == 0) {
	// LKTOKEN  _@M461_ = "Next turnpoint" 
	DoStatusMessage(gettext(TEXT("_@M461_")));
	if (EnableSoundModes) {
		LKSound(_T("LK_TASKPOINT.WAV"));
	}
	return;
  }

  if (_tcscmp(misc, TEXT("ORBITER")) == 0) {
	Orbiter=!Orbiter;
	if (Orbiter)
		DoStatusMessage(gettext(TEXT("_@M867_"))); // ORBITER ON
	else
		DoStatusMessage(gettext(TEXT("_@M868_"))); // ORBITER OFF
	return;
  }

  if (_tcscmp(misc, TEXT("TASKCONFIRMSTART")) == 0) {
	bool startTaskAnyway = false;
        if (EnableSoundModes) LKSound(_T("LK_TASKSTART.WAV"));
	dlgStartTaskShowModal(&startTaskAnyway, 
		CALCULATED_INFO.TaskStartTime,
		CALCULATED_INFO.TaskStartSpeed,
		CALCULATED_INFO.TaskStartAltitude);
	if (startTaskAnyway) {
		ActiveWayPoint=0; 
		StartTask(&GPS_INFO,&CALCULATED_INFO, true, true);
		// GCE_TASK_START does not work here, why?
		InputEvents::eventService(_T("TASKSTART"));
	}
	return;
  }

  if (_tcscmp(misc, TEXT("PROFILES")) == 0) {

	dlgProfilesShowModal();
	return;

  }

  if (_tcscmp(misc, TEXT("SHADING")) == 0) {
	Shading = !Shading;
	return;
  }
  if (_tcscmp(misc, TEXT("OVERLAYS")) == 0) {
	ToggleOverlays();
	return;
  }

  if (_tcscmp(misc, TEXT("LOCKMODE")) == 0) {
	TCHAR mtext[80];
	if (LockMode(1)) {
		// LKTOKEN  _@M960_ = "CONFIRM SCREEN UNLOCK?" 
		_tcscpy(mtext, gettext(_T("_@M960_")));
	} else {
		// LKTOKEN  _@M961_ = "CONFIRM SCREEN LOCK?" 
		_tcscpy(mtext, gettext(_T("_@M961_")));
	}
	if (MessageBoxX(hWndMapWindow, 
		mtext, _T(""),
		MB_YESNO|MB_ICONQUESTION) == IDYES) {
			if (LockMode(2)) { // invert LockMode
				if (ISPARAGLIDER)
					DoStatusMessage(gettext(_T("_@M962_"))); // SCREEN IS LOCKED UNTIL TAKEOFF
				DoStatusMessage(gettext(_T("_@M1601_"))); // DOUBLECLICK TO UNLOCK
			} else
				DoStatusMessage(gettext(_T("_@M964_"))); // SCREEN IS UNLOCKED
	}
	return;
  }

  if (_tcscmp(misc, TEXT("UTMPOS")) == 0) {
	extern void LatLonToUtmWGS84 (int& utmXZone, char& utmYZone, double& easting, double& northing, double lat, double lon);
	int utmzone; char utmchar;
	double easting, northing;
	TCHAR mbuf[80];
	#ifndef DISABLEAUDIO
	if (EnableSoundModes) PlayResource(TEXT("IDR_WAV_CLICK"));
	#endif
	LatLonToUtmWGS84 ( utmzone, utmchar, easting, northing, GPS_INFO.Latitude, GPS_INFO.Longitude );
	_stprintf(mbuf,_T("UTM %d%c  %.0f  %.0f"), utmzone, utmchar, easting, northing);
	Message::Lock();
	Message::AddMessage(60000, 1, mbuf);
	TCHAR sLongitude[16];
	TCHAR sLatitude[16];
	Units::LongitudeToString(GPS_INFO.Longitude, sLongitude, sizeof(sLongitude)-1);
	Units::LatitudeToString(GPS_INFO.Latitude, sLatitude, sizeof(sLatitude)-1);
	_stprintf(mbuf,_T("%s %s"), sLatitude, sLongitude);
	Message::AddMessage(60000, 1, mbuf);
	Message::Unlock();
	return;
  }

  if (_tcscmp(misc, TEXT("ORACLE")) == 0) {
	extern void dlgOracleShowModal(void);
	#ifndef DISABLEAUDIO
	if (EnableSoundModes) LKSound(TEXT("LK_BELL.WAV"));
	#endif
	dlgOracleShowModal();
	return;
  }

  if (_tcscmp(misc, TEXT("TOTALEN")) == 0) {
	UseTotalEnergy=!UseTotalEnergy;
	if (UseTotalEnergy)
		DoStatusMessage(gettext(TEXT("_@M1667_"))); // TOTAL ENERGY ON
	else
		DoStatusMessage(gettext(TEXT("_@M1668_"))); // TOTAL ENERGY OFF
	return;
  }

  if (_tcscmp(misc, TEXT("TERRCOL")) == 0) {
	if (TerrainRamp+1>13)
		TerrainRamp=0;  // 13 = NUMRAMPS -1
	else
		++TerrainRamp;
	MapWindow::RefreshMap();
	return;
  }


  // we should not get here
  DoStatusMessage(_T("Unknown Service: "),misc);

}

void InputEvents::eventChangeBack(const TCHAR *misc) { // 100114

  if (BgMapColor>=(LKMAXBACKGROUNDS-1))
	BgMapColor=0;
  else
	BgMapColor++;

  return;

}


#include "device.h"
#include "McReady.h"

// Bugs
// Adjusts the degradation of glider performance due to bugs
// up: increases the performance by 10%
// down: decreases the performance by 10%
// max: cleans the aircraft of bugs
// min: selects the worst performance (50%)
// show: shows the current bug degradation
void InputEvents::eventBugs(const TCHAR *misc) {
  double oldBugs = BUGS;

  LockComm(); // Must LockComm to prevent deadlock
  LockFlightData();

  if (_tcscmp(misc, TEXT("up")) == 0) {
    BUGS = iround(BUGS*100+10) / 100.0;
  } 
  if (_tcscmp(misc, TEXT("down")) == 0) {
    BUGS = iround(BUGS*100-10) / 100.0;
  }
  if (_tcscmp(misc, TEXT("max")) == 0) {
    BUGS= 1.0;
  }
  if (_tcscmp(misc, TEXT("min")) == 0) {
    BUGS= 0.0;
  }
  if (_tcscmp(misc, TEXT("show")) == 0) {
    TCHAR Temp[100];
    _stprintf(Temp,TEXT("%d"), iround(BUGS*100));
    DoStatusMessage(TEXT("Bugs Performance"), Temp);    
  } 
  if (BUGS != oldBugs) {
    BUGS= min(1.0,max(0.5,BUGS));
    
    devPutBugs(devA(), BUGS);
    devPutBugs(devB(), BUGS);
    GlidePolar::SetBallast();
  }
  UnlockFlightData();
  UnlockComm();
}

// Ballast
// Adjusts the ballast setting of the glider
// up: increases ballast by 10% 
// down: decreases ballast by 10%
// max: selects 100% ballast
// min: selects 0% ballast
// show: displays a status message indicating the ballast percentage
void InputEvents::eventBallast(const TCHAR *misc) {
  double oldBallast= BALLAST;
  LockComm(); // Must LockComm to prevent deadlock
  LockFlightData();
  if (_tcscmp(misc, TEXT("up")) == 0) {
    BALLAST = iround(BALLAST*100.0+10) / 100.0;
  } 
  if (_tcscmp(misc, TEXT("down")) == 0) {
    BALLAST = iround(BALLAST*100.0-10) / 100.0;
  } 
  if (_tcscmp(misc, TEXT("max")) == 0) {
    BALLAST= 1.0;
  } 
  if (_tcscmp(misc, TEXT("min")) == 0) {
    BALLAST= 0.0;
  } 
  if (_tcscmp(misc, TEXT("show")) == 0) {
    TCHAR Temp[100];
    _stprintf(Temp,TEXT("%d"),iround(BALLAST*100));
    DoStatusMessage(TEXT("Ballast %"), Temp);
  } 
  if (BALLAST != oldBallast) {
    BALLAST=min(1.0,max(0.0,BALLAST));
    devPutBallast(devA(), BALLAST);
    devPutBallast(devB(), BALLAST);
    GlidePolar::SetBallast();
  }
  UnlockFlightData();
  UnlockComm();
}

#include "Task.h"
#include "Logger.h"


void InputEvents::eventAutoLogger(const TCHAR *misc) {
  #if TESTBENCH
  #else
  if (SIMMODE) return;
  #endif
  if (!DisableAutoLogger) {
    eventLogger(misc);
  }
}

// Logger
// Activates the internal IGC logger
//  start: starts the logger 
// start ask: starts the logger after asking the user to confirm
// stop: stops the logger
// stop ask: stops the logger after asking the user to confirm
// toggle: toggles between on and off
// toggle ask: toggles between on and off, asking the user to confirm
// show: displays a status message indicating whether the logger is active
// nmea: turns on and off NMEA logging
// note: the text following the 'note' characters is added to the log file
void InputEvents::eventLogger(const TCHAR *misc) {

  TCHAR szMessage[MAX_PATH] = TEXT("\0");
  _tcsncpy(szMessage, TEXT(". eventLogger: "),MAX_PATH);
  _tcsncat(szMessage, misc,MAX_PATH);
  _tcsncat(szMessage,TEXT("\r\n"),MAX_PATH);
  StartupStore(szMessage);

  if (_tcscmp(misc, TEXT("start ask")) == 0) {
    guiStartLogger();
    return;
  } else if (_tcscmp(misc, TEXT("start")) == 0) {
    guiStartLogger(true);
    return;
  } else if (_tcscmp(misc, TEXT("stop ask")) == 0) {
    guiStopLogger();
    return;
  } else if (_tcscmp(misc, TEXT("stop")) == 0) {
    guiStopLogger(true);
    return;
  } else if (_tcscmp(misc, TEXT("toggle ask")) == 0) {
    guiToggleLogger();
    return;
  } else if (_tcscmp(misc, TEXT("toggle")) == 0) {
    guiToggleLogger(true);
    return;
  } else if (_tcscmp(misc, TEXT("nmea")) == 0) {
    EnableLogNMEA = !EnableLogNMEA;
    if (EnableLogNMEA) {
      // DoStatusMessage(TEXT("NMEA Log ON"));
      DoStatusMessage(gettext(TEXT("_@M864_"))); // NMEA Log ON
    } else {
      // DoStatusMessage(TEXT("NMEA Log OFF"));
      DoStatusMessage(gettext(TEXT("_@M865_"))); // NMEA Log OFF
    }
    return;
  } else if (_tcscmp(misc, TEXT("show")) == 0) {
    if (LoggerActive) {
      DoStatusMessage(gettext(TEXT("_@M862_"))); // Logger ON
    } else {
      DoStatusMessage(gettext(TEXT("_@M863_"))); // Logger OFF
    }
  } else if (_tcsncmp(misc, TEXT("note"), 4)==0) {
    // add note to logger file if available..
    LoggerNote(misc+4);
  }
}

// RepeatStatusMessage
// Repeats the last status message.  If pressed repeatedly, will
// repeat previous status messages
void InputEvents::eventRepeatStatusMessage(const TCHAR *misc) {
  (void)misc;
	// new interface
  // TODO enhancement: display only by type specified in misc field
  Message::Repeat(0);
}

// NearestAirspaceDetails
// Displays details of the nearest airspace to the aircraft in a 
// status message.  This does nothing if there is no airspace within
// 100km of the aircraft.
// If the aircraft is within airspace, this displays the distance and bearing
// to the nearest exit to the airspace.

#if USEOLDASPWARNINGS
bool dlgAirspaceWarningIsEmpty(void);

extern bool RequestAirspaceWarningForce;
#endif

void InputEvents::eventNearestAirspaceDetails(const TCHAR *misc) {
  (void)misc;
  double nearestdistance=0;
  double nearestbearing=0;

  // StartHourglassCursor();
  CAirspace *found = CAirspaceManager::Instance().FindNearestAirspace(GPS_INFO.Longitude, GPS_INFO.Latitude,
		      &nearestdistance, &nearestbearing );
  if (found != NULL) {
	dlgAirspaceDetails(found);
  }
}

// NearestWaypointDetails
// Displays the waypoint details dialog
//  aircraft: the waypoint nearest the aircraft
//  pan: the waypoint nearest to the pan cursor
void InputEvents::eventNearestWaypointDetails(const TCHAR *misc) {
  if (_tcscmp(misc, TEXT("aircraft")) == 0) {
    MapWindow::Event_NearestWaypointDetails(GPS_INFO.Longitude,
					    GPS_INFO.Latitude,
					    1.0e5, // big range..
					    false); 
  }
  if (_tcscmp(misc, TEXT("pan")) == 0) {
    MapWindow::Event_NearestWaypointDetails(GPS_INFO.Longitude,
					    GPS_INFO.Latitude,
					    1.0e5, // big range..
					    true); 
  }
}

// Null
// The null event does nothing.  This can be used to override
// default functionality
void InputEvents::eventNull(const TCHAR *misc) {
	(void)misc;
  // do nothing
}

// TaskLoad
// Loads the task of the specified filename
void InputEvents::eventTaskLoad(const TCHAR *misc) {
  TCHAR buffer[MAX_PATH];
  if (_tcslen(misc)>0) {
	LockTaskData();
	LocalPath(buffer,_T(LKD_TASKS));
	_tcscat(buffer,_T("\\"));
	_tcscat(buffer,misc);
	LoadNewTask(buffer);
	UnlockTaskData();
  }
}

// TaskSave
// Saves the task to the specified filename
void InputEvents::eventTaskSave(const TCHAR *misc) {
  TCHAR buffer[MAX_PATH];
  if (_tcslen(misc)>0) {
	LockTaskData();
	LocalPath(buffer,_T(LKD_TASKS));
	_tcscat(buffer,_T("\\"));
	_tcscat(buffer,misc);
	SaveTask(buffer);
	UnlockTaskData();
  }
}


extern void SettingsEnter();
extern void SettingsLeave();

// ProfileLoad
// Loads the profile of the specified filename
void InputEvents::eventProfileLoad(const TCHAR *misc) {
  TCHAR buffer[MAX_PATH];
  TCHAR buffer2[MAX_PATH];
  bool factory=false;
  if (_tcslen(misc)>0) {

	if (_tcscmp(misc,_T("Factory")) == 0) {
		if (MessageBoxX(hWndMapWindow, 
			_T("This will reset configuration to factory default. Confirm ?"), 
			_T("Reset Factory Profile"), 
			MB_YESNO|MB_ICONQUESTION) == IDNO) {
			return;
		}
		LocalPath(buffer,_T(LKD_SYSTEM)); // 100223
		_tcscat(buffer,_T("\\"));
		_tcscat(buffer,_T("FACTORYPRF"));
		factory=true;
	} else {
		LocalPath(buffer,_T(LKD_CONF)); // 100223
		_tcscat(buffer,_T("\\"));
		_tcscat(buffer,misc);
	}

	FILE *fp=NULL;
	if (_tcslen(buffer)>0) fp = _tfopen(buffer, TEXT("rb"));
	if(fp == NULL) {
		if (factory)
			_stprintf(buffer2,_T("Profile \"%s\" not found inside _System"),misc);
		else
			_stprintf(buffer2,_T("Profile \"%s\" not found inside _Configuration"),misc);
		MessageBoxX(hWndMapWindow, buffer2, _T("Load Profile"), MB_OK|MB_ICONEXCLAMATION);
		return;
	}
	fclose(fp);

	if (!factory) {
		_stprintf(buffer2,_T("Confirm loading \"%s\" from _Configuration ?"),misc);
		if (MessageBoxX(hWndMapWindow, 
			buffer2,
			_T("Load Profile"), 
			MB_YESNO|MB_ICONQUESTION) == IDNO) {
			return;
		}
	}

	SettingsEnter();
	ReadProfile(buffer);
	WAYPOINTFILECHANGED=true;
	SettingsLeave();
	_stprintf(buffer2,_T("Profile \"%s\" loaded"),misc);
	MessageBoxX(hWndMapWindow, buffer2, _T("Load Profile"), MB_OK|MB_ICONEXCLAMATION);
  }
}

// ProfileSave
// Saves the profile to the specified filename
void InputEvents::eventProfileSave(const TCHAR *misc) {
  TCHAR buffer[MAX_PATH];
  if (_tcslen(misc)>0) {
	_stprintf(buffer,_T("Confirm saving \"%s\" to _Configuration ?"),misc); 
	if (MessageBoxX(hWndMapWindow, 
		buffer, 
		_T("Save Profile"), 
		MB_YESNO|MB_ICONQUESTION) == IDNO) {
		return;
	}

	LocalPath(buffer,_T(LKD_CONF)); // 100223
	_tcscat(buffer,_T("\\"));
	_tcscat(buffer,misc);
	WriteProfile(buffer);
	_stprintf(buffer,_T("%s saved to _Configuration "),misc);
	MessageBoxX(hWndMapWindow, buffer, _T("Save Profile"), MB_OK|MB_ICONEXCLAMATION);
  }
}


void InputEvents::eventBeep(const TCHAR *misc) {
#ifndef DISABLEAUDIO
  if (EnableSoundModes) MessageBeep(MB_ICONEXCLAMATION); // 100221 FIX
#endif
}

void SystemConfiguration(void);

// Setup
// Activates configuration and setting dialogs
//  Basic: Basic settings (QNH/Bugs/Ballast/MaxTemperature)
//  Wind: Wind settings
//  Task: Task editor
//  Airspace: Airspace filter settings
//  Replay: IGC replay dialog
void InputEvents::eventSetup(const TCHAR *misc) {

  if (_tcscmp(misc,TEXT("Basic"))==0){
    dlgBasicSettingsShowModal();
  } else if (_tcscmp(misc,TEXT("Wind"))==0){
    dlgWindSettingsShowModal();
  } else if (_tcscmp(misc,TEXT("System"))==0){
    SystemConfiguration();
  } else if (_tcscmp(misc,TEXT("Task"))==0){
    dlgTaskOverviewShowModal();
  } else if (_tcscmp(misc,TEXT("Airspace"))==0){
    dlgAirspaceShowModal(false);
  } else if (_tcscmp(misc,TEXT("Replay"))==0){
    if (!GPS_INFO.MovementDetected) {     
      dlgLoggerReplayShowModal();
    }
#if USESWITCHES
  } else if (_tcscmp(misc,TEXT("Switches"))==0){
    dlgSwitchesShowModal();
#endif
  } else if (_tcscmp(misc,TEXT("AspAnalysis"))==0){
    dlgAnalysisShowModal(ANALYSIS_PAGE_AIRSPACE);
  } else if (_tcscmp(misc,TEXT("Teamcode"))==0){
    dlgTeamCodeShowModal();
  } else if (_tcscmp(misc,TEXT("Target"))==0){
    dlgTarget();
  }

}


// DLLExecute
// Runs the plugin of the specified filename
void InputEvents::eventDLLExecute(const TCHAR *misc) {
  // LoadLibrary(TEXT("test.dll"));

  StartupStore(TEXT("... %s%s"), misc,NEWLINE);
	
  TCHAR data[MAX_PATH];
  TCHAR* dll_name;
  TCHAR* func_name;
  TCHAR* other;
  TCHAR* pdest;
	
  _tcscpy(data, misc);

  // dll_name (up to first space)
  pdest = _tcsstr(data, TEXT(" "));
  if (pdest == NULL) {
    return;
  }
  *pdest = _T('\0');
  dll_name = data;

  // func_name (after first space)
  func_name = pdest + 1;

  // other (after next space to end of string)
  pdest = _tcsstr(func_name, TEXT(" "));
  if (pdest != NULL) {
    *pdest = _T('\0');
    other = pdest + 1;
  } else {
    other = NULL;
  }

  HINSTANCE hinstLib;	// Library pointer
  DLLFUNC_INPUTEVENT lpfnDLLProc = NULL;	// Function pointer

  // Load library, find function, execute, unload library
  hinstLib = _loadDLL(dll_name);
  if (hinstLib != NULL) {
#if !(defined(__MINGW32__)&&(WINDOWSPC>0))
    lpfnDLLProc = (DLLFUNC_INPUTEVENT)GetProcAddress(hinstLib, func_name);
#endif
    if (lpfnDLLProc != NULL) {
      (*lpfnDLLProc)(other);
    }
  }
}

// Load a DLL (only once, keep a cache of the handle)
//	TODO code: FreeLibrary - it would be nice to call FreeLibrary 
//      before exit on each of these
HINSTANCE _loadDLL(TCHAR *name) {
  int i;
  for (i = 0; i < DLLCache_Count; i++) {
    if (_tcscmp(name, DLLCache[i].text) == 0)
      return DLLCache[i].hinstance;
  }
  if (DLLCache_Count < MAX_DLL_CACHE) {
    DLLCache[DLLCache_Count].hinstance = LoadLibrary(name);
    if (DLLCache[DLLCache_Count].hinstance) {
      DLLCache[DLLCache_Count].text = StringMallocParse(name);
      DLLCache_Count++;
      
      // First time setup... (should check version numbers etc...)
      DLLFUNC_SETHINST lpfnDLLProc = NULL;
#if !(defined(__MINGW32__)&&(WINDOWSPC>0))
      lpfnDLLProc = (DLLFUNC_SETHINST)
	GetProcAddress(DLLCache[DLLCache_Count - 1].hinstance, 
		       TEXT("XCSAPI_SetHInst"));
#endif
      if (lpfnDLLProc)
	lpfnDLLProc(GetModuleHandle(NULL));
      
      return DLLCache[DLLCache_Count - 1].hinstance;
    }
  }

  return NULL;
}

// AdjustForecastTemperature
// Adjusts the maximum ground temperature used by the convection forecast
// +: increases temperature by one degree celsius
// -: decreases temperature by one degree celsius
// show: Shows a status message with the current forecast temperature
void InputEvents::eventAdjustForecastTemperature(const TCHAR *misc) {
  if (_tcscmp(misc, TEXT("+")) == 0) {
    CuSonde::adjustForecastTemperature(1.0);
  }
  if (_tcscmp(misc, TEXT("-")) == 0) {
    CuSonde::adjustForecastTemperature(-1.0);
  }
  if (_tcscmp(misc, TEXT("show")) == 0) {
    TCHAR Temp[100];
    _stprintf(Temp,TEXT("%f"),CuSonde::maxGroundTemperature);
    DoStatusMessage(TEXT("Forecast temperature"), Temp);
  }
}

// Run
// Runs an external program of the specified filename.
// Note that LK will wait until this program exits.
void InputEvents::eventRun(const TCHAR *misc) {
  bool doexec=false;
  TCHAR path[MAX_PATH];
  if (_tcscmp(misc, TEXT("ext1")) == 0) {
	LocalPath(path,_T("ext1.exe"));
	doexec=true;
  }
  if (_tcscmp(misc, TEXT("ext2")) == 0) {
	LocalPath(path,_T("ext2.exe"));
	doexec=true;
  }
  if (_tcscmp(misc, TEXT("ext3")) == 0) {
	LocalPath(path,_T("ext3.exe"));
	doexec=true;
  }
  if (_tcscmp(misc, TEXT("ext4")) == 0) {
	LocalPath(path,_T("ext4.exe"));
	doexec=true;
  }

  if (!doexec) {
	StartupStore(_T("..... Run type <%s> unknown%s"),misc,NEWLINE);
	return;
  }
  StartupStore(_T("..... Running <%s>%s"),path,NEWLINE);


  PROCESS_INFORMATION pi;
  STARTUPINFO si;
  ZeroMemory(&si,sizeof(STARTUPINFO));
  si.cb=sizeof(STARTUPINFO);
  si.wShowWindow= SW_SHOWNORMAL;
  si.dwFlags = STARTF_USESHOWWINDOW;
  // example if (!::CreateProcess(_T("C:\\WINDOWS\\notepad.exe"),_T(""), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
  if (!::CreateProcess(path,_T(""), NULL, NULL, FALSE, CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi)) {
	StartupStore(_T("... RUN FAILED%s"),NEWLINE);
	return;
  }

  ::WaitForSingleObject(pi.hProcess, INFINITE);
  StartupStore(_T("... RUN TERMINATED%s"),NEWLINE);
}

void InputEvents::eventDeclutterLabels(const TCHAR *misc) {
  if (_tcscmp(misc, TEXT("toggle")) == 0) {
	MapWindow::DeclutterLabels ++;
	MapWindow::DeclutterLabels = MapWindow::DeclutterLabels % 4;
  } else if (_tcscmp(misc, TEXT("on")) == 0)
    MapWindow::DeclutterLabels = MAPLABELS_ALLOFF;
  else if (_tcscmp(misc, TEXT("off")) == 0)
    MapWindow::DeclutterLabels = MAPLABELS_ALLON;
  else if (_tcscmp(misc, TEXT("mid")) == 0)
    MapWindow::DeclutterLabels = MAPLABELS_ONLYTOPO;
  else if (_tcscmp(misc, TEXT("show")) == 0) {
    if (MapWindow::DeclutterLabels==MAPLABELS_ALLON)
	// LKTOKEN  _@M422_ = "Map labels ON" 
      DoStatusMessage(gettext(TEXT("_@M422_")));
    else if (MapWindow::DeclutterLabels==MAPLABELS_ONLYTOPO)
	// LKTOKEN  _@M423_ = "Map labels TOPO" 
      DoStatusMessage(gettext(TEXT("_@M423_")));  
    else 
	// LKTOKEN  _@M421_ = "Map labels OFF" 
      DoStatusMessage(gettext(TEXT("_@M421_")));  
  }
}



#if 0 // REMOVE
void InputEvents::eventBrightness(const TCHAR *misc) {
	(void)misc;
#if 0
  dlgBrightnessShowModal();
#endif
}
#endif

void InputEvents::eventExit(const TCHAR *misc) {
	(void)misc;
  SendMessage(hWndMainWindow, WM_CLOSE,
	      0, 0);
}

void InputEvents::eventChangeTurn(const TCHAR *misc) {
  if (_tcscmp(misc, TEXT("left")) == 0){
	if (SimTurn<=-45) return;
	SimTurn-=5;
  }
  if (_tcscmp(misc, TEXT("right")) == 0){
	if (SimTurn>=45) return;
	SimTurn+=5;
  }
}

void InputEvents::eventChangeHGPS(const TCHAR *misc) {
  if (_tcscmp(misc, TEXT("up")) == 0){
	if (Units::GetUserAltitudeUnit() == unFeet)
		GPS_INFO.Altitude += 45.71999999;
	else
		GPS_INFO.Altitude += 50;
	return;
  }
  if (_tcscmp(misc, TEXT("down")) == 0){
	if (Units::GetUserAltitudeUnit() == unFeet)
		GPS_INFO.Altitude -= 45.71999999;
	else
		GPS_INFO.Altitude -= 50;
	if ((GPS_INFO.Altitude+40)<40) GPS_INFO.Altitude=0;
	return;
  }
}

// 10 Kmh
void InputEvents::eventChangeGS(const TCHAR *misc) {
double step=0;
  if (Units::GetUserHorizontalSpeedUnit() == unKnots)
	step=0.514444; //@ 1 knot, 1.8kmh  (1 knot is 1 nautical mile per hour)
  if (Units::GetUserHorizontalSpeedUnit() == unKiloMeterPerHour)
	step=0.27778; //@ 1 kmh 0.27777  has a rounding error
  if (Units::GetUserHorizontalSpeedUnit() == unStatuteMilesPerHour)
	step=0.44704; //@ 1 mph = 1.6kmh

  if (_tcscmp(misc, TEXT("up")) == 0){
	if (AircraftCategory == (AircraftCategory_t)umParaglider)
		GPS_INFO.Speed += step;
	else
		GPS_INFO.Speed += step*5;
	return;
  }
  if (_tcscmp(misc, TEXT("down")) == 0){
	if (AircraftCategory == (AircraftCategory_t)umParaglider)
		GPS_INFO.Speed -= step;
	else
		GPS_INFO.Speed -= step*5;
	if (GPS_INFO.Speed <0) GPS_INFO.Speed=0;
	return;
  }
}


void InputEvents::eventMoveGlider(const TCHAR *misc) {
  int i;
  if (_tcscmp(misc, TEXT("reset")) == 0){
	MapWindow::GliderScreenPositionX=50;
	MapWindow::GliderScreenPositionY=MapWindow::GliderScreenPosition;
	return; 
  }
  if (_tcscmp(misc, TEXT("down")) == 0){
	i=MapWindow::GliderScreenPositionY - 10; 
	// 20 is 20%, to avoid positioning on the bottom bar
	if (i <20) i=90;
	MapWindow::GliderScreenPositionY=i;
	return; 
  }
  if (_tcscmp(misc, TEXT("up")) == 0){
	i=MapWindow::GliderScreenPositionY + 10; 
	if (i >90) i=20;
	MapWindow::GliderScreenPositionY=i;
	return; 
  }
  if (_tcscmp(misc, TEXT("left")) == 0){
	i=MapWindow::GliderScreenPositionX - 10; 
	if (i <10) i=90;
	MapWindow::GliderScreenPositionX=i;
	return; 
  }
  if (_tcscmp(misc, TEXT("right")) == 0){
	i=MapWindow::GliderScreenPositionX + 10; 
	if (i >90) i=10;
	MapWindow::GliderScreenPositionX=i;
	return; 
  }

}

void InputEvents::eventUserDisplayModeForce(const TCHAR *misc){

  if (_tcscmp(misc, TEXT("unforce")) == 0){
    MapWindow::mode.UserForcedMode(MapWindow::Mode::MODE_FLY_NONE);
  }
  else if (_tcscmp(misc, TEXT("forceclimb")) == 0){
    MapWindow::mode.UserForcedMode(MapWindow::Mode::MODE_FLY_CIRCLING);
  }
  else if (_tcscmp(misc, TEXT("forcecruise")) == 0){
    MapWindow::mode.UserForcedMode(MapWindow::Mode::MODE_FLY_CRUISE);
  }
  else if (_tcscmp(misc, TEXT("forcefinal")) == 0){
    MapWindow::mode.UserForcedMode(MapWindow::Mode::MODE_FLY_FINAL_GLIDE);
  }
  else if (_tcscmp(misc, TEXT("show")) == 0){
    // DoStatusMessage(TEXT("Map labels ON")); 091211 ?????
  }

}

void InputEvents::eventAirspaceDisplayMode(const TCHAR *misc){

  if (_tcscmp(misc, TEXT("toggle")) == 0){
    if (++AltitudeMode >ALLOFF) AltitudeMode=ALLON;
	switch(AltitudeMode) {
		case 0:
	// LKTOKEN  _@M71_ = "Airspaces: ALL ON" 
			DoStatusMessage(gettext(TEXT("_@M71_")));
			break;
		case 1:
	// LKTOKEN  _@M73_ = "Airspaces: CLIPPED" 
			DoStatusMessage(gettext(TEXT("_@M73_")));
			break;
		case 2:
	// LKTOKEN  _@M72_ = "Airspaces: AUTO" 
			DoStatusMessage(gettext(TEXT("_@M72_")));
			break;
		case 3:
	// LKTOKEN  _@M69_ = "Airspaces: ALL BELOW" 
			DoStatusMessage(gettext(TEXT("_@M69_")));
			break;
		case 4:
	// LKTOKEN  _@M74_ = "Airspaces: INSIDE" 
			DoStatusMessage(gettext(TEXT("_@M74_")));
			break;
		case 5:
	// LKTOKEN  _@M70_ = "Airspaces: ALL OFF" 
			DoStatusMessage(gettext(TEXT("_@M70_")));
			break;
		default:
			break;
	}
  }
  if (_tcscmp(misc, TEXT("all")) == 0){
    AltitudeMode = ALLON;
  }
  else if (_tcscmp(misc, TEXT("clip")) == 0){
    AltitudeMode = CLIP;
  }
  else if (_tcscmp(misc, TEXT("auto")) == 0){
    AltitudeMode = AUTO;
  }
  else if (_tcscmp(misc, TEXT("below")) == 0){
    AltitudeMode = ALLBELOW;
  }
  else if (_tcscmp(misc, TEXT("off")) == 0){
    AltitudeMode = ALLOFF;
  }
}

// THIS IS UNUSED, AND SHOULD NOT BE USED SINCE IT DOES NOT SUPPORT CUPs
void InputEvents::eventAddWaypoint(const TCHAR *misc) {
  static int tmpWaypointNum = 0;
  WAYPOINT edit_waypoint;
  LockTaskData();
  edit_waypoint.Latitude = GPS_INFO.Latitude;
  edit_waypoint.Longitude = GPS_INFO.Longitude;
  edit_waypoint.Altitude = CALCULATED_INFO.TerrainAlt;
  edit_waypoint.FileNum = 2; // don't put into file
  edit_waypoint.Flags = 0;
  if (_tcscmp(misc, TEXT("landable")) == 0) {
    edit_waypoint.Flags += LANDPOINT;
  }
  edit_waypoint.Comment = NULL;
  edit_waypoint.Name[0] = 0;
  edit_waypoint.Details = 0;
  edit_waypoint.Number = NumberOfWayPoints;

  WAYPOINT *new_waypoint = GrowWaypointList();
  if (new_waypoint) {
    tmpWaypointNum++;
    memcpy(new_waypoint,&edit_waypoint,sizeof(WAYPOINT));
    wsprintf(new_waypoint->Name,TEXT("_%d"), tmpWaypointNum);
    new_waypoint->Details= 0;
  }
  UnlockTaskData();
}



void InputEvents::eventOrientation(const TCHAR *misc){
  if (_tcscmp(misc, TEXT("northup")) == 0){
    DisplayOrientation = NORTHUP;
  }
  else if (_tcscmp(misc, TEXT("northcircle")) == 0){
    DisplayOrientation = NORTHCIRCLE;
  }
  else if (_tcscmp(misc, TEXT("trackcircle")) == 0){
    DisplayOrientation = TRACKCIRCLE;
  }
  else if (_tcscmp(misc, TEXT("trackup")) == 0){
    DisplayOrientation = TRACKUP;
  }
  else if (_tcscmp(misc, TEXT("northtrack")) == 0){
    DisplayOrientation = NORTHTRACK;
  }
  else if (_tcscmp(misc, TEXT("northsmart")) == 0){ // 100417
    DisplayOrientation = NORTHSMART;
	/*
	if (InfoBoxLayout::landscape) 
		DisplayOrientation = NORTHSMART;
	else
		DisplayOrientation = NORTHUP;
	*/
  }
  MapWindow::SetAutoOrientation(true); // 101008 reset it
}


