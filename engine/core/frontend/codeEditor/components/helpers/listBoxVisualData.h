#ifndef _LISTBOX_VISUALDATA_H__
#define _LISTBOX_VISUALDATA_H__

#include <wx/wx.h>

//----------------------------------------------------------------------
// Helper classes for ListBox

// The class manages the colours, images, and other data needed for popup lists.
class ÑListBoxVisualData
{
public:
	ÑListBoxVisualData(int d);
	virtual ~ÑListBoxVisualData();

	// ListBoxImpl implementation
	void SetDesiredVisibleRows(int d);
	int  GetDesiredVisibleRows() const;
	void RegisterImage(int type, const wxBitmap& bmp);
	void RegisterImage(int, const char *);
	void RegisterRGBAImage(int, int, int, const unsigned char *);
	void ClearRegisteredImages();

	// Image data
	const wxBitmap* GetImage(int i) const;

	// Colour data
	void ComputeColours();
	const wxColour& GetBorderColour() const;
	void SetColours(const wxColour&, const wxColour&,
		const wxColour&, const wxColour&);
	const wxColour& GetBgColour() const;
	const wxColour& GetTextColour() const;
	const wxColour& GetHighlightBgColour() const;
	const wxColour& GetHighlightTextColour() const;

	// ListCtrl Style
	void UseListCtrlStyle(bool, const wxColour&, const wxColour&);
	bool HasListCtrlAppearance() const;
	const wxColour& GetCurrentBgColour() const;
	const wxColour& GetCurrentTextColour() const;

	// Data needed for SELECTION_CHANGE event
	void SetSciListData(int*, int*, int*);
	int GetListType() const;
	int GetPosStart() const;
	int GetStartLen() const;

private:
	
	WX_DECLARE_HASH_MAP(int, wxBitmap, wxIntegerHash, wxIntegerEqual, ImgList);

	int      m_desiredVisibleRows;
	ImgList  m_imgList;

	wxColour m_borderColour;
	wxColour m_bgColour;
	wxColour m_textColour;
	wxColour m_highlightBgColour;
	wxColour m_highlightTextColour;
	bool     m_useDefaultBgColour;
	bool     m_useDefaultTextColour;
	bool     m_useDefaultHighlightBgColour;
	bool     m_useDefaultHighlightTextColour;

	bool     m_hasListCtrlAppearance;
	wxColour m_currentBgColour;
	wxColour m_currentTextColour;
	bool     m_useDefaultCurrentBgColour;
	bool     m_useDefaultCurrentTextColour;

	int*     m_listType;
	int*     m_posStart;
	int*     m_startLen;
};

#endif 

