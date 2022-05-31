#ifndef _LUNA_TABART_H__
#define _LUNA_TABART_H__

#include <wx/aui/auibar.h>

class CAuiGenericToolBarArt : public wxAuiGenericToolBarArt
{
public:

	CAuiGenericToolBarArt();
	virtual ~CAuiGenericToolBarArt();

	virtual wxAuiToolBarArt* Clone() wxOVERRIDE;
	virtual void SetFlags(unsigned int flags) wxOVERRIDE;
	virtual unsigned int GetFlags() wxOVERRIDE;
	virtual void SetFont(const wxFont& font) wxOVERRIDE;
	virtual wxFont GetFont() wxOVERRIDE;
	virtual void SetTextOrientation(int orientation) wxOVERRIDE;
	virtual int GetTextOrientation() wxOVERRIDE;

	virtual void DrawBackground(
		wxDC& dc,
		wxWindow* wnd,
		const wxRect& rect) wxOVERRIDE;

	virtual void DrawPlainBackground(wxDC& dc,
		wxWindow* wnd,
		const wxRect& rect) wxOVERRIDE;

	virtual void DrawLabel(
		wxDC& dc,
		wxWindow* wnd,
		const wxAuiToolBarItem& item,
		const wxRect& rect) wxOVERRIDE;

	virtual void DrawButton(
		wxDC& dc,
		wxWindow* wnd,
		const wxAuiToolBarItem& item,
		const wxRect& rect) wxOVERRIDE;

	virtual void DrawDropDownButton(
		wxDC& dc,
		wxWindow* wnd,
		const wxAuiToolBarItem& item,
		const wxRect& rect) wxOVERRIDE;

	virtual void DrawControlLabel(
		wxDC& dc,
		wxWindow* wnd,
		const wxAuiToolBarItem& item,
		const wxRect& rect) wxOVERRIDE;

	virtual void DrawSeparator(
		wxDC& dc,
		wxWindow* wnd,
		const wxRect& rect) wxOVERRIDE;

	virtual void DrawGripper(
		wxDC& dc,
		wxWindow* wnd,
		const wxRect& rect) wxOVERRIDE;

	virtual void DrawOverflowButton(
		wxDC& dc,
		wxWindow* wnd,
		const wxRect& rect,
		int state) wxOVERRIDE;

	virtual wxSize GetLabelSize(
		wxDC& dc,
		wxWindow* wnd,
		const wxAuiToolBarItem& item) wxOVERRIDE;

	virtual wxSize GetToolSize(
		wxDC& dc,
		wxWindow* wnd,
		const wxAuiToolBarItem& item) wxOVERRIDE;

	virtual int GetElementSize(int element) wxOVERRIDE;
	virtual void SetElementSize(int elementId, int size) wxOVERRIDE;

	virtual int ShowDropDown(wxWindow* wnd,
		const wxAuiToolBarItemArray& items) wxOVERRIDE;

	virtual void UpdateColoursFromSystem() wxOVERRIDE;
};

#endif