////////////////////////////////////////////////////////////////////////////
//	Description : The query constructor - the text pane, the parse gate, and the way in
//	              (queryConstructor.h)
////////////////////////////////////////////////////////////////////////////

#include "queryConstructorInternal.h"

using namespace queryctor;

// ===========================================================================
//  The text pane, the check, and OK — all one gate
// ===========================================================================

bool ibDialogQueryConstructor::AdoptText(const wxString& text, bool reportModally)
{
	// THE ENGINE JUDGES. Not a checker of our own, not a lenient pre-pass — the same parser the
	// runtime runs, and whatever it throws is what the user reads.
	try {
		ibQueryParser parser;
		ibQueryPackage parsed = parser.ParsePackage(text);
		if (parsed.m_statements.empty()) {
			ibQueryAstStatement statement;
			statement.m_select = std::make_shared<ibQuerySelect>();
			statement.m_select->m_selectAll = true;
			parsed.m_statements.push_back(statement);
		}
		m_package = parsed;
		if (m_statement >= m_package.m_statements.size())
			m_statement = m_package.m_statements.size() - 1;
		FillSourceTree();
		FillAll();
		return true;
	}
	catch (const ibBackendException& e) {
		// SAID ONCE, in the place that is already saying it. The verdict line under the text
		// carries the engine's words continuously; popping a modal with the same sentence every
		// time focus leaves a half-typed query turns the one useful message into an obstacle.
		// A modal is for a moment the user ASKED about — Check, or OK.
		if (reportModally)
			wxMessageBox(e.GetErrorDescription(), _("Query"), wxOK | wxICON_ERROR, this);
		else
			ShowEngineError(e.GetErrorDescription());
		return false;
	}
}

// Put a message on the verdict line without re-parsing — used when the text in the pane is what
// failed, so the line must show ITS error rather than the last good package's clean bill.
void ibDialogQueryConstructor::ShowEngineError(const wxString& message)
{
	if (m_status == nullptr)
		return;
	m_status->SetForegroundColour(wxColour(0xC0, 0x30, 0x30));
	m_status->SetLabelText(message);
	m_status->Refresh();
}

void ibDialogQueryConstructor::OnPreviewFocusLost(wxFocusEvent& event)
{
	event.Skip();
	if (m_filling || m_preview == nullptr || !CanEdit())
		return;

	const wxString typed = m_preview->GetText();
	if (typed == ibRenderQueryPackage(m_package))
		return;   // nothing was typed — do not re-parse text we wrote ourselves

	// A syntax error leaves the TEXT alone and says so on the verdict line. Rewriting what somebody
	// just typed because it does not parse YET is how a constructor loses work, and a modal every
	// time they click away is how it becomes something people close.
	AdoptText(typed, /*reportModally*/ false);
}

void ibDialogQueryConstructor::OnCheck(wxCommandEvent&)
{
	// A SELECTION IS CHECKED ON ITS OWN. In a long query the interesting question is usually about
	// one piece of it, and reading the engine's line-and-position against the whole text is how a
	// person loses the place. Nothing else changes: the same parser answers, in its own words.
	wxString selection = m_preview != nullptr ? m_preview->GetSelectedText() : wxString();
	selection.Trim(true).Trim(false);   // Trim MUTATES — a const copy cannot be trimmed in place
	if (!selection.IsEmpty()) {
		try {
			ibQueryParser parser;
			parser.ParsePackage(selection);
			wxMessageBox(_("No errors found in the selection."), _("Check query"), wxOK | wxICON_INFORMATION, this);
		}
		catch (const ibBackendException& e) {
			wxMessageBox(e.GetErrorDescription(), _("Check query"), wxOK | wxICON_ERROR, this);
		}
		return;
	}

	// The button asks the engine the same question OK asks, and hands back the same answer — the
	// point of having it is that a person can ask whenever they like, not only when they are done.
	wxString message;
	if (AskEngine(message))
		wxMessageBox(_("No errors found."), _("Check query"), wxOK | wxICON_INFORMATION, this);
	else
		wxMessageBox(message, _("Check query"), wxOK | wxICON_ERROR, this);
}

void ibDialogQueryConstructor::OnOpenSelection(wxCommandEvent&)
{
	const wxString selection = m_preview != nullptr
		? m_preview->GetSelectedText().Trim(true).Trim(false) : wxString();
	if (selection.IsEmpty()) {
		wxMessageBox(_("Select a part of the query text first: that part opens as a query of its own."),
			_("Open the selection"), wxOK | wxICON_INFORMATION, this);
		return;
	}

	// AND IT IS EDITABLE, because the fragment DOES know where it goes: it is the selection, and the
	// selection is a span in this very control. Opening it to be read only was the timid answer —
	// "look at this piece with its own tabs" is half of what a person wants when they point at a
	// subquery; the other half is to fix it there and have it land back where it was.
	//
	// A fragment wrapped in its parentheses (`(SELECT …)`, as an IN or a nested table is written) is
	// unwrapped for the parser and re-wrapped on the way back, so the surrounding text stays valid.
	wxString text = selection;
	const bool parenthesised = text.StartsWith(wxT("(")) && text.EndsWith(wxT(")"));
	if (parenthesised)
		text = text.Mid(1, text.length() - 2).Trim(true).Trim(false);

	if (!ibShowQueryConstructor(this, text, m_metaData, m_readOnly) || !CanEdit())
		return;

	long from = 0, to = 0;
	from = m_preview->GetSelectionStart();
	to   = m_preview->GetSelectionEnd();
	if (from == to)
		return;   // the selection went away while the window was open — write nothing blindly

	m_preview->SetSelection(from, to);
	m_preview->ReplaceSelection(parenthesised ? wxT("(") + text + wxT(")") : text);
	AdoptText(m_preview->GetText());
}

void ibDialogQueryConstructor::OnOk(wxCommandEvent&)
{
	if (!CanEdit()) {
		EndModal(wxID_CANCEL);   // nothing was edited; nothing is handed back
		return;
	}

	// Whatever is in the text pane is what the user means — take it first, so a hand-edit that was
	// never followed by a click on another control is not thrown away.
	if (m_preview != nullptr && m_preview->GetText() != ibRenderQueryPackage(m_package)) {
		if (!AdoptText(m_preview->GetText()))
			return;
	}

	// The last word belongs to the engine: render, and hand the text to the parser. What comes back
	// IS what leaves this dialog, so the caller can never receive text the engine has not read.
	try {
		ibQueryParser parser;
		m_package = parser.ParsePackage(ibRenderQueryPackage(m_package));
	}
	catch (const ibBackendException& e) {
		wxMessageBox(e.GetErrorDescription(), _("Query"), wxOK | wxICON_ERROR, this);
		return;
	}

	EndModal(wxID_OK);
}

wxString ibDialogQueryConstructor::GetText() const
{
	return ibRenderQueryPackage(m_package);
}

void ibDialogQueryConstructor::SetSubQueryMode()
{
	m_subQuery = true;

	// The package TAB goes: a nested table is not a list of statements, and an "Add query" there
	// would offer to build something a source cannot hold.
	if (m_notebook != nullptr) {
		for (size_t page = m_notebook->GetPageCount(); page > 0; --page)
			if (m_notebook->GetPageText(page - 1) == _("Query batch"))
				m_notebook->RemovePage(page - 1);
	}

	// FOR UPDATE goes with it — it holds a STATEMENT's rows, and a sub-query is not one. Hidden
	// rather than greyed, because a disabled control says "not now" while this is "not here".
	// (The kind and its name live on the strip, so they left with it.)
	if (m_forUpdate != nullptr)
		m_forUpdate->Hide();

	Layout();
}

// ===========================================================================
//  Entry points
// ===========================================================================

bool ibShowQueryConstructor(wxWindow* parent, wxString& queryText, const ibMetaData* metaData, bool readOnly)
{
	ibQueryPackage package;
	if (!queryText.Trim(true).Trim(false).IsEmpty()) {
		// Text that does not parse is still worth opening on — but not silently: the author is told
		// what the engine could not read, and the constructor starts from a blank query rather than
		// pretending it understood.
		try {
			ibQueryParser parser;
			package = parser.ParsePackage(queryText);
		}
		catch (const ibBackendException& e) {
			if (wxMessageBox(wxString(e.GetErrorDescription()) + wxT("\n\n")
					+ _("Open the constructor on an empty query? The existing text will be replaced only if you press OK."),
					_("Query constructor"), wxYES_NO | wxICON_WARNING, parent) != wxYES)
				return false;
			package = ibQueryPackage();
		}
	}

	ibDialogQueryConstructor dialog(parent, package, metaData, readOnly);
	if (dialog.ShowModal() != wxID_OK)
		return false;

	queryText = dialog.GetText();
	return true;
}

bool ibShowQueryConstructorFor(wxWindow* parent, ibQuerySelectPtr& select, const ibMetaData* metaData, bool readOnly)
{
	// ONE SELECT, edited in place. A nested table and a union branch are both this: a query
	// standing inside another, so they open the same window one level down rather than a second
	// editor that would have to be kept in step with this one.
	ibQueryPackage package;
	ibQueryAstStatement statement;
	statement.m_select = select ? select : std::make_shared<ibQuerySelect>();
	package.m_statements.push_back(statement);

	ibDialogQueryConstructor dialog(parent, package, metaData, readOnly);
	dialog.SetSubQueryMode();   // a query inside another one — no package, no INTO, no FOR UPDATE
	if (dialog.ShowModal() != wxID_OK)
		return false;   // read-only closes this way too — nothing is written back, by construction

	const ibQuerySelectPtr edited = dialog.GetPackage().SingleSelect();
	if (!edited)
		return false;   // the author turned the sub-query into a package; a nested table cannot be one
	select = edited;
	return true;
}
