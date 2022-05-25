#ifndef _TOOLBAR_H_
#define _TOOLBAR_H_

#include <wx/aui/auibar.h>

class CAuiToolBar : public wxAuiToolBar
{
public:

	CAuiToolBar() : wxAuiToolBar() {}

	CAuiToolBar(wxWindow* parent,
		wxWindowID id = wxID_ANY,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize,
		long style = wxAUI_TB_DEFAULT_STYLE) : wxAuiToolBar(parent, id, pos, size, style) {}

	wxAuiToolBarItem* InsertTool(int idx, int tool_id,
		const wxString& label,
		const wxBitmap& bitmap,
		const wxBitmap& disabledBitmap,
		wxItemKind kind,
		const wxString& shortHelpString,
		const wxString& longHelpString,
		wxObject* WXUNUSED(client_data))
	{
		wxAuiToolBarItem item;

		item.SetWindow(NULL);
		item.SetLabel(label);
		item.SetBitmap(bitmap);
		item.SetDisabledBitmap(disabledBitmap);
		item.SetShortHelp(shortHelpString);
		item.SetLongHelp(longHelpString);
		item.SetActive(true);
		item.SetHasDropDown(false);
		item.SetId(tool_id);
		item.SetState(0);
		item.SetProportion(0);
		item.SetKind(kind);
		item.SetSizerItem(NULL);
		item.SetMinSize(wxDefaultSize);
		item.SetUserData(0);
		item.SetSticky(false);

		if (tool_id == wxID_ANY) item.SetId(wxNewId());

		if (!disabledBitmap.IsOk())
		{
			// no disabled bitmap specified, we need to make one
			if (bitmap.IsOk())
			{
				item.SetDisabledBitmap(bitmap.ConvertToDisabled());
			}
		}

		m_items.Insert(item, idx);
		return &m_items[idx];
	}

	wxAuiToolBarItem* InsertSeparator(int idx, int tool_id)
	{
		wxAuiToolBarItem item;

		item.SetWindow(NULL);
		item.SetLabel(wxEmptyString);
		item.SetBitmap(wxNullBitmap);
		item.SetDisabledBitmap(wxNullBitmap);
		item.SetActive(true);
		item.SetHasDropDown(false);
		item.SetId(tool_id);
		item.SetState(0);
		item.SetProportion(0);
		item.SetKind(wxITEM_SEPARATOR);
		item.SetSizerItem(NULL);
		item.SetMinSize(wxDefaultSize);
		item.SetUserData(0);
		item.SetSticky(false);

		if (tool_id == wxID_ANY) item.SetId(wxNewId());

		m_items.Insert(item, idx);
		return &m_items[idx];
	}
};

#endif 
