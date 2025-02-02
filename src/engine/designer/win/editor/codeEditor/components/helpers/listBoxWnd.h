#ifndef _LISTBOX_H__
#define _LISTBOX_H__

#include <wx/wx.h>
#include <wx/hashset.h>
#include <wx/vlbox.h>
#include <wx/listctrl.h>
#include "wx/toplevel.h"
#include <wx/tokenzr.h>
#include <wx/renderer.h>
#include <wx/wupdlock.h>

#include "listBoxVisualData.h"

// The class is intended to look like a standard listbox (with an optional
// icon). However, it needs to look like it has focus even when it doesn't.
class COESListBox : public wxSystemThemedControl<wxVListBox>
{
public:
	COESListBox(wxWindow*, ÑListBoxVisualData*, int);

	// wxWindow overrides
	virtual bool AcceptsFocus() const override;
	virtual void SetFocus() override;

	// Setters
	void SetContainerBorderSize(int);

	// ListBoxImpl implementation
	void SetListBoxFont(wxFont &font);
	void SetAverageCharWidth(int width);
	wxRect GetDesiredRect() const;
	int CaretFromEdge() const;
	void Clear();
	void Append(wxString &s, int type = -1);
	int Length() const;
	void Select(int n);
	wxString GetValue(int n) const;

protected:
	
	// Helpers
	void AppendHelper(const wxString& text, int type);
	void AccountForBitmap(int type, bool recalculateItemHeight);
	void RecalculateItemHeight();
	int TextBoxFromClientEdge() const;

	// Event handlers
	void OnSysColourChanged(wxSysColourChangedEvent& event);
	void OnMouseMotion(wxMouseEvent& event);
	void OnMouseLeaveWindow(wxMouseEvent& event);

	// wxVListBox overrides
	virtual wxCoord OnMeasureItem(size_t) const override;
	virtual void OnDrawItem(wxDC&, const wxRect &, size_t) const override;
	virtual void OnDrawBackground(wxDC&, const wxRect&, size_t) const override;

private:

	WX_DECLARE_HASH_SET(int, wxIntegerHash, wxIntegerEqual, SetOfInts);

	ÑListBoxVisualData* m_visualData;
	wxVector<wxString>      m_labels;
	wxVector<int>           m_imageNos;
	size_t                  m_maxStrWidth;
	int                     m_currentRow;
	int                     m_aveCharWidth;

	// These drawing parameters are computed or set externally.
	int m_borderSize;
	int m_textHeight;
	int m_itemHeight;
	int m_textTopGap;
	int m_imageAreaWidth;
	int m_imageAreaHeight;

	// These drawing parameters are set internally and can be changed if needed
	// to better match the appearance of a list box on a specific platform.
	int m_imagePadding;
	int m_textBoxToTextGap;
	int m_textExtraVerticalPadding;
};

#endif