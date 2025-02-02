#ifndef _FORM_DEFS_H__
#define _FORM_DEFS_H__

#include <wx/wx.h>

class Property;
class Event;

class PropertyCategory;

// Used to identify wxObject* that must be manually deleted
class wxNoObject : public wxObject {};

#define COMPONENT_TYPE_FRAME		 1
#define COMPONENT_TYPE_WINDOW		 2
#define COMPONENT_TYPE_SIZER		 3
#define COMPONENT_TYPE_SIZERITEM	 4

#endif //_FORMDEFS_H__
