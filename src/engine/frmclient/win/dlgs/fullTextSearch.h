#ifndef _FRMCLIENT_FULL_TEXT_SEARCH_H__
#define _FRMCLIENT_FULL_TEXT_SEARCH_H__

#include <wx/dialog.h>
#include <wx/listctrl.h>

#include "frmclient/frmclient.h"
#include "protocol/protocolNode.h"

class wxButton;
class wxCheckBox;
class wxGauge;
class wxStaticText;
class wxTextCtrl;

// THE BASE'S FULL-TEXT SEARCH INDEX - Administration → Full-text search index: the mode switched, the index built and
// cleared, as the server's schema holds them (frmserver/schema/runtime/fullTextSearch.cpp). The build goes a portion
// per call, so the window stays alive and Stop is heard between portions; a stopped build is told how far it got.
class FRMCLIENT_API ibDialogFullTextSearch : public wxDialog {
public:

	ibDialogFullTextSearch(wxWindow* parent, const ibProtocolNode& shown);

private:

	// What the server says now, read into the window.
	void Show(const ibProtocolNode& shown);
	// The schema asked again; false - refused, and the person told why.
	bool Refresh();
	// One of the schema's commands; false - refused, and the person told why.
	bool Command(int command, bool withEnabled = false, bool enabled = false);

	void OnEnabled(wxCommandEvent& event);
	void OnUpdate(wxCommandEvent& event);
	void OnStop(wxCommandEvent& event);
	void OnClear(wxCommandEvent& event);
	void OnClose(wxCloseEvent& event);

	wxCheckBox*   m_enabled = nullptr;
	wxStaticText* m_state = nullptr;
	wxStaticText* m_updated = nullptr;
	wxGauge*      m_gauge = nullptr;
	wxButton*     m_update = nullptr;
	wxButton*     m_stop = nullptr;
	wxButton*     m_clear = nullptr;
	wxButton*     m_close = nullptr;

	bool      m_running = false;
	bool      m_stopAsked = false;
	bool      m_indexTrue = false;
	long long m_indexed = 0;
	long long m_total = 0;
};

// FIND IN DATA - the records holding every word of a text, among those the person may read; a double click opens one
// in its form. Not modal: the record opens beside it, and the person goes on to the next.
class FRMCLIENT_API ibDialogSearch : public wxDialog {
public:

	ibDialogSearch(wxWindow* parent);

private:

	void OnFind(wxCommandEvent& event);
	void OnOpen(wxListEvent& event);
	void OnClose(wxCloseEvent& event);

	wxTextCtrl*   m_text = nullptr;
	wxStaticText* m_found = nullptr;
	wxListCtrl*   m_hits = nullptr;
};

#endif
