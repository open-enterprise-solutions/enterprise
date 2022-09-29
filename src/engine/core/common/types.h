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
	PT_NULL,

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
	PT_NUMBER,
	PT_WXSTRING_I18N,
	PT_PARENT,
	PT_CLASS,
	PT_EDIT_OPTION,

	PT_TYPE_DATA,
	PT_OWNER_DATA,
	PT_RECORD_DATA,
	PT_ACTION_DATA,
	PT_SOURCE_DATA,
	PT_GENERATION_DATA,

	PT_MAX

} PropertyType;

/**
 * Lista de enteros.
 */
class IntList {
	std::vector<int> m_ints; bool m_abs;
public:

	IntList(bool absolute_value = false) : m_abs(absolute_value) {}
	IntList(wxString value, bool absolute_value = false);

	unsigned int GetSize() { return (unsigned int)m_ints.size(); }
	int GetValue(unsigned int idx) { return m_ints[idx]; }

	void Add(int value);
	void DeleteList();
	void SetList(const wxString& str);
	wxString ToString();
};

#endif