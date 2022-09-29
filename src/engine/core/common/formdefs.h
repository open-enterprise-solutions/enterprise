#ifndef _FORMDEFS_H__
#define _FORMDEFS_H__

#include <map>
#include <memory>
#include <vector>

#include <wx/wx.h>

class IPropertyObject;
class IValueFrame;

class IMetadata; 

class Property;
class Event;

class PropertyCategory;

// Used to identify wxObject* that must be manually deleted
class wxNoObject : public wxObject {};

// Flatnotebook styles are stored in config, if style #defines change, or config is manually modified, these style overrides still apply
#define FNB_STYLE_OVERRIDES( x ) ( x | wxFNB_CUSTOM_DLG | wxFNB_NO_X_BUTTON ) & ( ~wxFNB_X_ON_TAB & ~wxFNB_MOUSE_MIDDLE_CLOSES_TABS & ~wxFNB_DCLICK_CLOSES_TABS & ~wxFNB_ALLOW_FOREIGN_DND )
 
#define COMPONENT_TYPE_ABSTRACT		 0
#define COMPONENT_TYPE_FRAME		 1
#define COMPONENT_TYPE_WINDOW		 2
#define COMPONENT_TYPE_WINDOW_TABLE  3
#define COMPONENT_TYPE_TABLE_COLUMN  4
#define COMPONENT_TYPE_SIZER		 5
#define COMPONENT_TYPE_SIZERITEM	 6
#define COMPONENT_TYPE_MENUBAR		 7
#define COMPONENT_TYPE_MENUITEM		 8
#define COMPONENT_TYPE_METADATA		 9

#endif //_FORMDEFS_H__
