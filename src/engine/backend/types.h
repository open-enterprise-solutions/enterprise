#ifndef _TYPES_H__
#define _TYPES_H__

#include <vector>
#include <map>
#include <memory>

#include <wx/wx.h>
#include <wx/string.h>
#include <wx/tokenzr.h>

#define COMPONENT_TYPE_ABSTRACT		 0
#define COMPONENT_TYPE_METADATA		 COMPONENT_TYPE_ABSTRACT

// **
// * Tipos de propiedades.
// */
typedef enum {
	
	PT_NULL,
	PT_BOOL,
	PT_TEXT,
	PT_INT,
	PT_UINT,
	PT_BITLIST,
	PT_INTLIST,
	PT_UINTLIST,
	PT_OPTION,
	PT_WXNAME,
	PT_WXSTRING,
	PT_WXPOINT,
	PT_WXSIZE,
	PT_WXFONT,
	PT_WXCOLOUR,
	PT_PATH,
	PT_FILE,
	PT_BITMAP,
	PT_STRINGLIST,
	PT_NUMBER,

	PT_EDIT_OPTION,

	PT_TYPE,
	PT_OWNER,
	PT_RECORD,
	PT_SOURCE,
	PT_GENERATION,

	PT_MAX

} PropertyType;

typedef enum {
	
	ET_EVENT,
	ET_ACTION,

	ET_MAX

} EventType;

#endif