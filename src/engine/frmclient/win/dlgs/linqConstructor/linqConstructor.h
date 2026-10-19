#ifndef __LINQ_CONSTRUCTOR_DLG_H__
#define __LINQ_CONSTRUCTOR_DLG_H__

////////////////////////////////////////////////////////////////////////////
// The LINQ CONSTRUCTOR — a query block built at a place in a module, out of what is visible there.
////////////////////////////////////////////////////////////////////////////
//
// NOT THE QUERY CONSTRUCTOR WITH ANOTHER SPELLING. A query text is data — a string with a parser and
// a writer of its own. A LINQ block is CODE, standing at a place in a module, and what it can read is
// what that place can see: the configuration's tables through `Data.`, and the module's own
// variables — a table filled above, the result of another query, the object's own tabular section.
// It LOOKS like the query constructor (tabs over the whole window, a tree of what can be read, lists
// with their own small toolbars, arrows between), because a person should not have to learn two.
//
// ⭐ SO THE WINDOW IS INTELLISENSE, LAID OUT. Everything in the source tree is asked of the same three
// doors the editor's dropdown is (backend/compiler/scriptComplete.h), at the place the window opened:
//   * what is visible there                           — ibNamesAtCaret
//   * what an expression offers after a dot           — ibValueAtCaret on `expression.`
//   * what a ROW of a source offers                   — ibValueAtCaret inside a probe query,
//                                                        `From linqRow In <source> Where linqRow.`
// A source whose rows the walk cannot describe (a table whose columns are added at run time, an
// argument of no known type) is still a source — its fields are written in by hand, on the author's
// responsibility, and the compiler's verdict under the block says what it made of them.
//
// ⭐ AND THE BLOCK IS THE LANGUAGE'S. The parts are an ibLinqBlock (core/compiler/blockSyntaxLINQ.h),
// written with the lexer's own words and checked against the table the completion offers clauses
// from; a query the caret stands in is OPENED — found by the compiler (ibOutlineScriptQueries), read
// back by ibLinqBlock::Parse — and replaced on OK.
//
// ⚠ IN THE CLIENT THE THREE DOORS AND THE OUTLINE ARE THE SERVER'S — the compiler and the module are there, and
// they are asked of the module's tab (ibProtocol::ibView::ibModule::ibLinq): the window's opening once, the places
// a tree opens once each (the place, its rows and what its members are, in one question), the compiler's reading of
// the block once per block it has not read. Each answer is kept for the window's life, so a question asked again —
// a probe, the same block — asks nothing.
//
////////////////////////////////////////////////////////////////////////////

#include "frmclient/frmclient.h"
#include "core/compiler/blockSyntaxLINQ.h"

#include <wx/dialog.h>
#include <wx/treectrl.h>
#include <wx/textctrl.h>
#include <wx/checkbox.h>
#include <wx/stattext.h>

#include <functional>
#include <map>
#include <vector>

class wxNotebook;
class wxToolBar;
class ibTabDocument;
class ibProtocolNode;

class FRMCLIENT_API ibDialogLinqConstructor : public wxDialog
{
public:

	// `text` is the module's text and `caret` the CHARACTER position the window was opened at; `module` — the desktop's
	// module — is the module's tab, asked what the compiler says (null: nothing is asked); `codeStyle` is the syntax the
	// module is written in (its tab's State).
	ibDialogLinqConstructor(wxWindow* parent, const wxString& text, unsigned int caret,
		ibTabDocument* module, short codeStyle, bool readOnly);

	// The parts, as the tabs hold them.
	ibLinqBlock Collect() const;

	// The block as it will be written; empty — with `refusal` saying why — when it cannot be.
	wxString GetBlock(wxString& refusal) const;

	// The stretch of the module the block REPLACES — the query the caret stood in when the window
	// opened — in characters. False when there was none, and the block goes in at the caret.
	bool GetReplacedSpan(unsigned int& from, unsigned int& to) const;

private:

	// One answer of IntelliSense: whether the expression resolved at all, what it resolved to, and the
	// names it offers after a dot.
	struct ibProbe {
		bool                  m_resolved = false;
		wxString              m_type;
		std::vector<wxString> m_names;
	};
	ibProbe Ask(const wxString& probe) const;
	ibProbe MembersOf(const wxString& expression) const;
	ibProbe RowOf(const wxString& expression) const;

	// ---- the module's tab, asked (the client's half of the desktop's three doors) ----
	// The probes MembersOf and RowOf ask — the statement alone: the tab puts the module's text before it.
	wxString MembersProbe(const wxString& expression) const;
	wxString RowProbe(const wxString& expression) const;
	// One question of the tab (Kind Linq, Text, Position, and the parts `request` names); its Values kept.
	bool AskModule(ibProtocolNode& request, const wxString& text, unsigned int position, ibProtocolNode& answer) const;
	void KeepValues(const ibProtocolNode& answer) const;
	// The probes not known yet, in one question; those in `deep` with their names' probes too.
	void AskAll(const std::vector<wxString>& probes, const std::vector<wxString>& deep) const;
	// The compiler's reading of `text` — the module with the block in it — asked and shown under the block.
	void AskVerdict(const wxString& text);

	// A value that is only a value - a number, a string, a date, a flag: a leaf, which goes into a
	// condition and is read as nothing. An expression the walk could not resolve is NOT one: it may be
	// an argument whose fields the author writes in by hand.
	static bool IsPlainValue(const ibProbe& members);

	// ---- the trees: one per tab that picks from what can be read, all asked the same way ----
	wxTreeCtrl* MakeSourceTree(wxWindow* parent);
	void AddPlace(wxTreeCtrl* tree, const wxTreeItemId& parent, const wxString& name, const wxString& expression,
		int icon, bool probe);
	void FillFields(wxTreeCtrl* tree, const wxTreeItemId& item, const wxString& expression, const ibProbe& row);
	void OnBeginDrag(wxTreeEvent& event);
	int  IconIndex(wxTreeCtrl* tree, const wxString& key, const std::function<wxIcon()>& make);
	int  KindIcon(wxTreeCtrl* tree, const wxString& section);
	void OnExpanding(wxTreeEvent& event);
	void OnActivated(wxTreeEvent& event);
	wxTreeCtrl* CurrentTree() const;

	// ---- the verbs: the arrows, the toolbars and a double click all come here ----
	void AddSelectedAsSource(bool join);
	void AddSelectedAsField();
	void AddSelectedToCondition();
	void AddSelectedAsGroupKey();
	void AddSelectedAsTotal();
	void AddSelectedToOrder();
	void WriteFieldByHand(wxTreeCtrl* tree, const wxTreeItemId& hint);

	wxString QualifiedField(const wxString& sourceExpression, const wxString& field);
	void AddSource(const wxString& expression, bool join);
	wxString AliasOf(const wxString& sourceExpression) const;
	wxString NewAlias(const wxString& expression) const;

	// The field the selected tree node stands for, as the block writes it (`o.Amount`), and a column
	// name for it no column of the answer has yet. False, with a bell, for a node that is not one.
	bool SelectedField(wxString& expression, wxString& name);
	// A group needs a name before its columns can be written: the first key gives it one.
	void NameTheGroup();

	// The tabs, filled from a block read back.
	void Open(const ibLinqBlock& block);

	wxToolBar* MakeBar(wxWindow* parent);
	void AddTool(wxToolBar* bar, const wxString& label, const wxString& artId, std::function<void()> action);

	// Continuation lines of the block line up under where it starts.
	wxString IndentAt(unsigned int position) const;

	// The block as it stands and the compiler's reading of it; ShowBlock — the block alone, while "Group into" is typed
	// (the module with the block in it, when the compiler has not read it yet; empty: nothing to ask).
	void ShowResult();
	wxString ShowBlock();
	void OnOk(wxCommandEvent& event);

	wxString                 m_text;
	unsigned int             m_caret = 0;
	unsigned int             m_anchor = 0;     // where the block will stand: the query opened, or the caret
	bool                     m_replacing = false;
	unsigned int             m_replaceFrom = 0;
	unsigned int             m_replaceTo = 0;
	ibTabDocument*           m_module = nullptr;   // the module's tab — what the compiler is asked through
	short                    m_codeStyle;
	std::vector<wxString>    m_variables;      // the names visible at the anchor that hold a value
	std::vector<bool>        m_variableOpens;  // …and whether each is a place to open, or a plain value
	std::vector<wxString>    m_sections;       // `Data`'s members - Catalogs, Documents, the registers

	wxNotebook*                                   m_notebook = nullptr;
	std::vector<wxTreeCtrl*>                      m_pageTrees;   // per page; null where a page has none
	std::map<wxTreeCtrl*, std::map<wxString, int>> m_treeIcons;

	// The platform's own list control (frmclient/win/ctrls/dataview) — every list of the constructors is
	// one, edited in place.
	class ibDataViewListCtrl* m_sources = nullptr;      // alias | reads | join | on | equals
	class ibDataViewListCtrl* m_fields = nullptr;       // column | expression
	class ibDataViewListCtrl* m_conditions = nullptr;   // condition | on groups
	class ibDataViewListCtrl* m_order = nullptr;        // expression | descending
	class ibDataViewListCtrl* m_groupKeys = nullptr;    // column | expression
	class ibDataViewListCtrl* m_totals = nullptr;       // column | total | expression
	wxTextCtrl*               m_groupInto = nullptr;

	// `Skip` / `Take`: switched on by the word, a counter with arrows - and a variable read out of a
	// block opened, kept as written until the counter is touched.
	struct ibRowCount {
		wxCheckBox*       m_use = nullptr;
		class wxSpinCtrl* m_count = nullptr;
		wxStaticText*     m_kept = nullptr;
		wxString          m_variable;
	};
	ibRowCount                m_skipCount;
	ibRowCount                m_takeCount;
	void KeepCount(ibRowCount& count, const wxString& variable);
	void OpenCount(ibRowCount& count, const wxString& text);
	static wxString CountText(const ibRowCount& count);

	wxCheckBox*               m_distinct = nullptr;
	wxStaticText*             m_fieldsNote = nullptr;   // on the fields tab: what the columns are when grouped

	// The block, shown by the code editor itself — the same colouring the module has.
	class ibCodeEditor*       m_block = nullptr;
	wxStaticText*             m_verdict = nullptr;

	// What the tab answered, kept for the window's life: each probe by its text, each section's picture by its name,
	// and the block last read with what the compiler read of it.
	mutable std::map<wxString, ibProbe> m_asked;
	std::map<wxString, wxString>        m_kindPictures;
	wxString                            m_verdictText;   // the module with the block in, as last asked
	wxString                            m_verdictLabel;  // …and the line it was answered with
	bool                                m_verdictRead = false;
};

#endif
