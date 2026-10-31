#ifndef __IB_AUTOCOMPLETE_H__
#define __IB_AUTOCOMPLETE_H__

#include "helpers/popupWnd.h"

/**
 */
class ibAutoComplete
{
	struct ibKeywordElement {
		short m_type;
		wxString m_name;
		wxString m_shortDescription;
	};

	enum
	{
		eVariable = 0,
		eExportVariable,
		eProcedure,
		eExportProcedure,
		eFunction,
		eExportFunction,
	};

public:

	ibAutoComplete(wxStyledTextCtrl* textCtrl);
	virtual ~ibAutoComplete();

	/// Is the auto completion list displayed?
	bool Active() const;

	/// Display the auto completion list positioned to be near a character position
	void Start(const wxString& currentWord, int position,
		int startLen, int lineHeight);

	/// The append string contains a sequence of words separated by the separator character
	void Append(short type, const wxString& strName, const wxString& sDescription);

	/// Return the position of the currently selected list item
	int GetSelection() const;

	/// Return the value of an item in the list
	wxString GetValue(int item) const;

	void Show(const wxPoint& position);
	void Cancel();

	/// Move the current list element by delta, scrolling appropriately
	void MoveUp();
	void MoveDown();

	/// Select a list element that starts with word as the current element
	void Select(int index);

	///Process event
	bool CallEvent(wxEvent& event);

protected:

	wxBitmap GetImageByType(short type) const;

	// Event handlers
	void OnSelection(wxCommandEvent& event);
	void OnKeyDown(wxKeyEvent& event);
	void OnMouseMotion(wxMouseEvent& event);

	void OnProcessFocus(wxFocusEvent& event);
	void OnProcessChildFocus(wxChildFocusEvent& event);
	void OnProcessSize(wxSizeEvent& event);
	void OnProcessMouse(wxMouseEvent& event);

private:

	bool                          m_active           = false;
	int                           m_posStart         = 0;
	int                           m_startLen         = 0;
	wxString                      m_currentWord;

	std::vector<ibKeywordElement> m_aKeywords;

	ibCodeEditorListBoxWin*              m_listBoxWin       = nullptr;
	ibListBoxVisualData*          m_visualData       = nullptr;
	wxStyledTextCtrl*             m_owner            = nullptr;
	wxEvtHandler*                 m_evtHandler       = nullptr;

	/// Should autocompletion be canceled if editor's currentPos <= startPos?
	bool                          m_cancelAtStartPos = true;
	bool                          m_dropRestOfWord   = false;
	int                           m_widthLBDefault   = 0;
	int                           m_heightLBDefault  = 0;

	/** SC_ORDER_PRESORTED:   Assume the list is presorted; selection will fail if it is not alphabetical<br />
	 *  SC_ORDER_PERFORMSORT: Sort the list alphabetically; start up performance cost for sorting<br />
	 *  SC_ORDER_CUSTOM:      Handle non-alphabetical entries; start up performance cost for generating a sorted lookup table
	 */
	int                           m_autoSort         = 0;
};

#endif
