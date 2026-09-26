#ifndef __CHOICE_PARAMETERS_DLG_H__
#define __CHOICE_PARAMETERS_DLG_H__

// The visual editor for ibChoiceParametersDescription — what is shown in the list when this field is
// chosen into. A row says three things: which parameter of the target it fills, where its value comes
// from, and what becomes of an already chosen value when that source changes.
//
// It edits a BUFFER, not the property: the description is copied in when the dialog opens and written
// back only on OK, so Cancel really cancels — the shape the schedule editor beside it uses.
//
// ⭐ ON THE HOUSE GRID, AND EDITED IN THE CELL. Both panes are ibDataViewCtrl over the constructor's
// own ibQueryGridModel — the model whose header says it exists to be the one grid model for every
// plain list. Two consequences, and the second is the point: a field is drawn with its own PICTURE, so
// a row reads as the kind of thing it is; and the Field and On-change cells drop down IN PLACE, so the
// two boxes that used to sit under the table are gone (Max, 2026-09-23: "dataview + attribute icons,
// edited — the editor inside").
//
// ⭐ NOTHING IS TYPED HERE. Both halves of a row are chosen from lists the BACKEND property answers
// (ibPropertyChoiceParameters), because the designer knows the target's fields and this field's
// neighbours at that moment, and a name typed by hand that matches nothing applies nothing while
// saying nothing about it (docs/private/choice-links.md § 2a).
//
// 🛑 AND THE LISTS IT ANSWERED ARE KEPT, not asked for and thrown away. The first cut drew each row by
// looking the parameter's NAME up again in the configuration — which finds no PREDEFINED field (Ref,
// Data version, Code, Parent are not ordinary metaobjects), so every row read as a bare "Filter." and
// the used ones were never taken off the offer. The catalogue below IS that answer, held: one source
// for the name, the picture and the filtering, so the three cannot disagree.

#include <wx/dialog.h>
#include <wx/button.h>
#include <wx/stattext.h>

#include <map>

#include "frontend/win/ctrls/dataview/dataview.h"
#include "backend/choiceLinkDescription.h"

class BACKEND_API ibPropertyChoiceParameters;

class ibDialogChoiceParameters : public wxDialog {
public:

	ibDialogChoiceParameters(wxWindow* parent, ibPropertyChoiceParameters* property,
		const ibChoiceParametersDescription& params);

	// The edited value — read after ShowModal() returns wxID_OK.
	const ibChoiceParametersDescription& GetParameters() const { return m_params; }

	// The designer entry: open the editor against a parameters property and hand back whether the rows
	// changed. Mirrors ibDialogJobSchedule::ShowScheduleDialog.
	static bool ShowChoiceParametersDialog(ibPropertyChoiceParameters* property,
		ibChoiceParametersDescription& params);

	// ⭐⭐ OK GOES THROUGH OR IT REFUSES, and a row with no parameter is what it refuses on. Such a row
	// names nothing, so it would narrow nothing and sit in the table looking like a setting — the
	// silently-wrong-value class this whole arc exists to avoid. The refusal names the row and puts the
	// cursor on it, because "something is wrong somewhere" is not an answer (Max, 2026-09-23: "the name
	// must always be filled — either complain at once, or do not let it be saved").
	virtual bool Validate() override;

private:

	// ONE OFFERED THING — what it is called, what it is drawn as, and what it means. A parameter is
	// named by id (a field of the target); a source by PATH, because a neighbour may be a column of a
	// tabular section and a number would name the leaf while losing the way to it.
	struct ibOffered {
		ibMetaID            m_id = 0;
		ibSourceDescription m_path;
		wxString            m_label;
		wxIcon              m_icon;
	};

	void BuildControls();
	void BuildCatalogue();

	// The parts of the window BuildControls puts together: the attributes on the left, the rows with
	// their order arrows on the right, and the two drags between and inside them.
	wxSizer* BuildAvailablePane();
	wxSizer* BuildRowsPane();
	void BindDragAndDrop();

	// What a row's cell says, and a word written into one — the rows grid's reader and writer.
	wxString RowCellText(unsigned int row, unsigned int col) const;
	bool SetRowCellText(unsigned int row, unsigned int col, const wxString& text);

	void FillAvailable();
	void FillRows();
	void ShowSelection();

	// ⭐ THE TWO EDITS EVERY ROAD INTO THE TABLE MAKES — a row added from an attribute (">", a double-click,
	// a drop) and a row moved (the arrows, a drag inside the table). Written once, so the roads cannot
	// leave the table in two different states.
	void AddRow(size_t available, long at);
	void MoveRow(long from, long to);

	void OnAdd(wxCommandEvent&);
	void OnRemove(wxCommandEvent&);
	void OnMoveUp(wxCommandEvent&);
	void OnMoveDown(wxCommandEvent&);

	// The line the right-hand grid has selected, or -1.
	long SelectedLine() const;
	void SelectLine(long line);

	// A row taking its value from this attribute, filled in as far as the catalogue can fill it.
	ibChoiceParameterRowDescription NewRow(size_t source) const;

	// Which parameters this attribute could fill — the backend's answer, kept because the left pane
	// asks it for every attribute on every refill.
	const std::vector<ibMetaID>& FitsOf(size_t source) const;

	// Open the Name cell of a row whose parameter was NOT obvious — the question, asked where it is
	// answered. Does nothing for a row that came filled in.
	void AskTheName(long line);

	// What the catalogue says about an id / a path / a label; null when it says nothing.
	const ibOffered* ParameterOf(const ibMetaID& id) const;
	const ibOffered* ParameterNamed(const wxString& label) const;
	const ibOffered* SourceOf(const ibSourceDescription& path) const;
	const ibOffered* SourceNamed(const wxString& label) const;

	ibPropertyChoiceParameters*   m_property = nullptr;
	ibChoiceParametersDescription m_params;              // the buffer

	// WHAT THE PROPERTY ANSWERED, HELD — see the note at the top of this file.
	std::vector<ibOffered> m_parameters;   // the fields of what this one refers to — offered in the NAME cell
	std::vector<ibOffered> m_sources;      // the fields standing beside it — offered in the LEFT pane
	wxArrayString          m_parameterWords;   // …the two as the cells' drop-downs spell them
	wxArrayString          m_sourceWords;
	wxString               m_targets;      // what the parameters belong to
	wxString               m_holder;       // …and what the attributes belong to, for the column's title

	// What each attribute could fill, asked once per attribute — see FitsOf.
	mutable std::map<ibMetaID, std::vector<ibMetaID>> m_fits;

	// THE PICTURE AN ORDINARY FIELD CARRIES, taken from the lists themselves. A predefined field (Ref,
	// Data version, Code, Parent) carries none, and drawn with none it came up as an EMPTY ROW — the
	// grid's icon-text cell cannot read a plain string. It is still a field, and is drawn as one.
	wxIcon m_fieldIcon;

	// left: THIS object's attributes not yet carried over — the SOURCE side, as the reference
	// implementation has it. Held as indices into m_sources.
	ibDataViewCtrl*         m_available      = nullptr;
	class ibQueryGridModel* m_availableModel = nullptr;
	std::vector<size_t>     m_availableSources;

	// ⭐ AND WHEN THERE IS NOTHING TO OFFER, IT SAYS SO. An empty window explains nothing, and the
	// commonest way to reach one is perfectly legitimate: the field refers to nothing, so there is no
	// list for a parameter to narrow (Max, 2026-09-23, opening this on a Quantity).
	wxStaticText* m_nothing = nullptr;

	// right: the rows, one line each — every cell but the name is edited where it stands
	ibDataViewCtrl*         m_rows      = nullptr;
	class ibQueryGridModel* m_rowsModel = nullptr;
	wxButton*               m_up        = nullptr;   // the order is the author's — it is what a reader sees
	wxButton*               m_down      = nullptr;
};

#endif // __CHOICE_PARAMETERS_DLG_H__
