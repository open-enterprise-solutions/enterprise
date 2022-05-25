#ifndef _TYPES_H__
#define _TYPES_H__

#include <vector>
#include <map>
#include <memory>

#include <wx/wx.h>
#include <wx/string.h>

// **
// * Tipos de propiedades.
// */
typedef enum
{
	PT_ERROR,
	PT_BOOL,
	PT_TEXT,
	PT_INT,
	PT_UINT,
	PT_BITLIST,
	PT_INTLIST,
	PT_UINTLIST,
	PT_OPTION,
	PT_MACRO,
	PT_WXNAME,
	PT_WXSTRING,
	PT_WXPOINT,
	PT_WXSIZE,
	PT_WXFONT,
	PT_WXCOLOUR,
	PT_WXPARENT,
	PT_WXPARENT_SB,
	PT_PATH,
	PT_FILE,
	PT_BITMAP,
	PT_STRINGLIST,
	PT_FLOAT,
	PT_WXSTRING_I18N,
	PT_PARENT,
	PT_CLASS,
	PT_EDIT_OPTION,
	
	PT_TYPE_SELECT,
	PT_OWNER_SELECT,
	PT_RECORD_SELECT,
	PT_TOOL_ACTION,
	PT_SOURCE_DATA

} PropertyType;

/**
 * Lista de enteros.
 */
class IntList
{
	bool m_abs;

	std::vector<int> m_ints;
	
public:
	
	IntList(bool absolute_value = false) : m_abs(absolute_value) {}
	IntList(wxString value, bool absolute_value = false);

	unsigned int GetSize() { return (unsigned int)m_ints.size(); }
	int GetValue(unsigned int idx) { return m_ints[idx]; }

	void Add(int value);
	void DeleteList();
	void SetList(const wxString &str);
	wxString ToString();
};

#endif