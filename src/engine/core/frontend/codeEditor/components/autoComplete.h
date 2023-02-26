#ifndef AUTOCOMPLETE_H__
#define AUTOCOMPLETE_H__

#include "helpers/popupWnd.h"

/**
 */
class CAutoComplete
{
	bool active;

	struct keywordElement_t {
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

	std::vector< keywordElement_t> m_aKeywords;

public:

	int posStart;
	int startLen;

	wxString sCurrentWord;

	COESListBoxWin *lb;
	ÑListBoxVisualData* m_visualData;
	wxStyledTextCtrl *m_owner;

	wxEvtHandler *m_evtHandler;

	/// Should autocompletion be canceled if editor's currentPos <= startPos?
	bool cancelAtStartPos;
	bool dropRestOfWord;
	int widthLBDefault;
	int heightLBDefault;

	/** SC_ORDER_PRESORTED:   Assume the list is presorted; selection will fail if it is not alphabetical<br />
	 *  SC_ORDER_PERFORMSORT: Sort the list alphabetically; start up performance cost for sorting<br />
	 *  SC_ORDER_CUSTOM:      Handle non-alphabetical entries; start up performance cost for generating a sorted lookup table
	 */
	int autoSort;

	CAutoComplete(wxStyledTextCtrl *textCtrl);
	virtual ~CAutoComplete();

	/// Is the auto completion list displayed?
	bool Active() const;

	/// Display the auto completion list positioned to be near a character position
	void Start(const wxString &sCurrentWord, int position,
		int startLen_, int lineHeight);

	/// The append string contains a sequence of words separated by the separator character
	void Append(short type, const wxString &sName, const wxString &sDescription);

	/// Return the position of the currently selected list item
	int GetSelection() const;

	/// Return the value of an item in the list
	wxString GetValue(int item) const;

	void Show(const wxPoint &position);
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
};

#endif
