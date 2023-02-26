#ifndef _FORMDEFS_H__
#define _FORMDEFS_H__

#include <map>
#include <memory>
#include <vector>

#include <wx/wx.h>

class IMetadata;

class IPropertyObject;

class IValueFrame;
class IControlFrame;

class Property;
class Event;

class PropertyCategory;

// Used to identify wxObject* that must be manually deleted
class wxNoObject : public wxObject {};

#define COMPONENT_TYPE_ABSTRACT		 0
#define COMPONENT_TYPE_FRAME		 1
#define COMPONENT_TYPE_WINDOW		 2
#define COMPONENT_TYPE_SIZER		 3
#define COMPONENT_TYPE_SIZERITEM	 4

#endif //_FORMDEFS_H__
