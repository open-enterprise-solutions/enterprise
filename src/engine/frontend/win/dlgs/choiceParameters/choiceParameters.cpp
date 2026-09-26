#include "choiceParameters.h"

#include "backend/propertyManager/property/propertyChoiceLink.h"
#include "backend/propertyManager/property/variant/variantChoiceLink.h"   // ibChoiceHolderName — what the fields belong to
#include "backend/metaData.h"

#include "frontend/win/dlgs/queryConstructor/queryGridModel.h"   // the one grid model for a plain list
#include "frontend/win/ctrls/dataview/dataviewEditOnActivate.h"  // …and the one way a cell opens on a double-click

#include <wx/sizer.h>
#include <wx/statline.h>
#include <wx/artprov.h>
#include <wx/msgdlg.h>   // the refusal OK gives when a row has no parameter
#include <wx/dnd.h>

namespace {

// The answers the third column offers, in the order they are meant to be read: the safe one first.
// The words name what becomes of the VALUE, not what the link does — see the enum's own note.
wxArrayString OnChangeWords()
{
	wxArrayString words;
	words.Add(_("Clear"));        // ibChoiceParameterOnChange::Clear
	words.Add(_("Keep"));         // …Keep
	return words;
}

// ⭐ `Filter.` IS WHAT A PARAMETER IS, not decoration: the row is a condition over a field of the
// target, and a parameter of another nature would be another table rather than another prefix. Spelled
// in one place because it is written into the cell AND read back out of it.
const wxString kFilter = wxT("Filter.");

// WHAT IS BEING DRAGGED, in the one format the fork's drag source speaks. An attribute coming from the
// left pane and a row being reordered are different acts, so the text says which — a drop that could
// not tell them apart would add a row where it was asked to move one.
const wxString kDragAttribute = wxT("choice-attribute:");
const wxString kDragRow       = wxT("choice-row:");

// 🛑 A ROW WITHOUT A PICTURE DREW ITS NAME BLANK, and that is not a look — it is the cell showing
// nothing at all. The grid model hands an icon-text VALUE only when it has an icon, and falls back to a
// plain string; an icon-text renderer cannot read a string, so every predefined field (Ref, Data
// version, Code, Parent — none of them carry a picture) came up empty (Max, 2026-09-23, screenshots).
//
// So the picture is guaranteed here rather than hoped for: a field with none is still a FIELD and is
// drawn as one.
wxIcon FieldPicture(const wxIcon& own, const wxIcon& ordinary)
{
	if (own.IsOk())
		return own;
	if (ordinary.IsOk())
		return ordinary;
	return wxArtProvider::GetIcon(wxART_NORMAL_FILE, wxART_MENU, wxSize(16, 16));
}

}

ibDialogChoiceParameters::ibDialogChoiceParameters(wxWindow* parent, ibPropertyChoiceParameters* property,
	const ibChoiceParametersDescription& params)
	: wxDialog(parent, wxID_ANY, _("Choice parameter links"), wxDefaultPosition, wxSize(820, 440),
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
	, m_property(property), m_params(params)
{
	// THE CATALOGUE FIRST: the grids are built from it, and it is what the property answered.
	BuildCatalogue();
	BuildControls();

	// ⚠ NOTHING IS SEEDED HERE. The owner's row is written when the field's TYPE is settled and is
	// ordinary data by the time this opens — so an author who deletes it has deleted it, which a window
	// re-offering it whenever it opens could never allow (propertyChoiceLink.h, ibChoiceOwnerRow).
	FillAvailable();
	FillRows();
	ShowSelection();
}

// ⭐ ASKED ONCE, HELD — see the note at the top of the header. Both panes, their labels and their
// pictures come from here, so a row cannot be drawn by one answer and filtered by another.
void ibDialogChoiceParameters::BuildCatalogue()
{
	if (m_property == nullptr)
		return;

	const auto keep = [](std::vector<ibOffered>& into, const ibPropertyChoiceList& list,
		unsigned int idx, const ibSourceDescription& path) {
			ibOffered offered;
			offered.m_id = (ibMetaID)list.GetId(idx);
			offered.m_path = path;
			offered.m_label = list.GetName(idx);
			const wxBitmap& picture = list.GetBitmap(idx);
			if (picture.IsOk())
				offered.m_icon.CopyFromBitmap(picture);
			into.push_back(offered);
	};

	// THE PARAMETERS — the fields of what this one refers to, PER TYPE, because a composite field refers
	// to more than one thing and they do not have the same fields. With one target type the prefix would
	// be noise. These are offered in the NAME cell, as `Filter.<field>`.
	ibPropertyChoiceList targets;
	ibFieldReferenceTypes(m_property->GetPropertyObject(), targets);

	for (unsigned int type = 0; type < targets.GetCount(); type++) {
		m_targets += (m_targets.IsEmpty() ? wxT("") : wxT(", ")) + targets.GetName(type);

		ibPropertyChoiceList fields;
		m_property->GetParameterList(reference_to_clsid((ibMetaID)targets.GetId(type)), fields);
		for (unsigned int idx = 0; idx < fields.GetCount(); idx++) {
			keep(m_parameters, fields, idx, ibSourceDescription());
			if (targets.GetCount() > 1)
				m_parameters.back().m_label = targets.GetName(type) + wxT(".") + m_parameters.back().m_label;
			m_parameterWords.Add(kFilter + m_parameters.back().m_label);
		}
	}

	// THE ATTRIBUTES — the fields standing beside this one, which supply the values. A neighbour is one
	// hop from the holder they both belong to, and the holder is NAMED for the pane that lists them.
	m_holder = ibChoiceHolderName(m_property->GetPropertyObject());

	ibPropertyChoiceList sources;
	m_property->GetSourceList(sources);
	for (unsigned int idx = 0; idx < sources.GetCount(); idx++) {
		keep(m_sources, sources, idx, ibSourceDescription((ibMetaID)sources.GetId(idx)));
		m_sourceWords.Add(m_sources.back().m_label);
	}

	// THE PICTURE AN ORDINARY FIELD CARRIES — taken from the lists themselves rather than named here, so
	// it is whatever the tree draws today. The predefined fields, which carry none, are drawn with it.
	for (const ibOffered& offered : m_sources) {
		if (offered.m_icon.IsOk()) { m_fieldIcon = offered.m_icon; break; }
	}
	if (!m_fieldIcon.IsOk()) {
		for (const ibOffered& offered : m_parameters) {
			if (offered.m_icon.IsOk()) { m_fieldIcon = offered.m_icon; break; }
		}
	}
}

void ibDialogChoiceParameters::BuildControls()
{
	wxBoxSizer* outer = new wxBoxSizer(wxVERTICAL);

	// The sentence the window opens with — what these rows DO, said once here rather than guessed from
	// three column headings. It names the two halves in the order the row reads: the parameter, then the
	// attribute the value comes from.
	outer->Add(new wxStaticText(this, wxID_ANY,
		_("The parameters used when a value is chosen into this field. A row gives the parameter's name "
		  "and the attribute to take its value from.")),
		wxSizerFlags().Expand().Border(wxALL, FromDIP(6)));

	// ⭐ AND THE ONE CASE WHERE THERE IS NOTHING TO SAY, SAID. A field that refers to nothing has no
	// list to narrow, which is an answer rather than a fault — but an empty window is not that answer.
	m_nothing = new wxStaticText(this, wxID_ANY,
		_("This field refers to nothing, so there is no list for a parameter to narrow. Give it a reference type first."));
	m_nothing->Show(m_parameters.empty());
	outer->Add(m_nothing, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(6)));

	wxBoxSizer* middle = new wxBoxSizer(wxHORIZONTAL);
	middle->Add(BuildAvailablePane(), wxSizerFlags(1).Expand().Border(wxALL, FromDIP(6)));

	// the two buttons between the panes, the way this shape is always drawn
	wxBoxSizer* between = new wxBoxSizer(wxVERTICAL);
	wxButton* add = new wxButton(this, wxID_ADD, wxT(">"), wxDefaultPosition, FromDIP(wxSize(32, 26)));
	wxButton* remove = new wxButton(this, wxID_REMOVE, wxT("<"), wxDefaultPosition, FromDIP(wxSize(32, 26)));
	between->AddStretchSpacer();
	between->Add(add, wxSizerFlags().Border(wxBOTTOM, FromDIP(4)));
	between->Add(remove, wxSizerFlags());
	between->AddStretchSpacer();
	middle->Add(between, wxSizerFlags().Expand().Border(wxTOP, FromDIP(24)));

	middle->Add(BuildRowsPane(), wxSizerFlags(3).Expand().Border(wxALL, FromDIP(6)));

	outer->Add(middle, wxSizerFlags(1).Expand());

	outer->Add(new wxStaticLine(this), wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT, FromDIP(6)));
	outer->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), wxSizerFlags().Expand().Border(wxALL, FromDIP(6)));

	SetSizer(outer);

	add->Bind(wxEVT_BUTTON, &ibDialogChoiceParameters::OnAdd, this);
	remove->Bind(wxEVT_BUTTON, &ibDialogChoiceParameters::OnRemove, this);
	m_up->Bind(wxEVT_BUTTON, &ibDialogChoiceParameters::OnMoveUp, this);
	m_down->Bind(wxEVT_BUTTON, &ibDialogChoiceParameters::OnMoveDown, this);

	m_rows->Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, [this](ibDataViewEvent&) { ShowSelection(); });
	m_available->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED, [this](ibDataViewEvent&) {
		wxCommandEvent unused; OnAdd(unused);   // a double-click is the same act as ">"
	});

	// ⭐⭐ A DOUBLE-CLICK OPENS THE CELL — the one helper the constructors arrived at after four
	// attempts (dataviewEditOnActivate.h), not a fifth copy of that reasoning. Without it a cell opens
	// only on F2 or on a second click of an already-selected row, which reads as "the grid needs some
	// number of clicks" (Max, 2026-09-23: "use the hack so it opens quickly").
	//
	// ⚠ AND IT IS WHY THE RIGHT PANE HAS NO DOUBLE-CLICK VERB OF ITS OWN. Removing a row on activate
	// stood here for one draft and would have fought the editor for the same event — the cell opening
	// and the row vanishing under it. The "<" button removes; a double-click edits.
	ibDataViewEditOnActivate(m_rows);

	BindDragAndDrop();
}

// ---- left: THIS object's attributes, the ones not yet used — see the note inside.
wxSizer* ibDialogChoiceParameters::BuildAvailablePane()
{
	// ⭐⭐ THE LEFT PANE IS THE SOURCE SIDE, and this was the wrong way round for a day. It listed the
	// fields of what this one REFERS TO, so a person standing in a document read a catalogue's Code and
	// Parent there and asked, three times, where the value is pulled from — because in the shape this
	// window has always had, the left pane IS "where from" (Max, 2026-09-23, showing the reference
	// implementation). The parameter is named in the cell; the attribute is what you pick and carry over.
	wxBoxSizer* left = new wxBoxSizer(wxVERTICAL);
	left->Add(new wxStaticText(this, wxID_ANY, _("Available attributes:")), wxSizerFlags().Border(wxBOTTOM, FromDIP(2)));

	m_available = new ibDataViewCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxDV_ROW_LINES | wxDV_SINGLE | wxDV_NO_HEADER);
	m_availableModel = new ibQueryGridModel();
	m_availableModel->SetReader([this](unsigned int row, unsigned int) -> wxString {
		return row < m_availableSources.size() ? m_sources[m_availableSources[row]].m_label : wxString();
	});
	m_availableModel->SetIconColumn(kGridCol1, FieldPicture(wxNullIcon, m_fieldIcon));
	m_availableModel->SetIconReader([this](unsigned int row) -> wxIcon {
		return FieldPicture(row < m_availableSources.size()
			? m_sources[m_availableSources[row]].m_icon : wxNullIcon, m_fieldIcon);
	});
	m_available->AssociateModel(m_availableModel);
	m_available->GetRootColumnGroup()->AppendColumn(new ibDataViewColumn(_("Attribute"),
		new ibDataViewIconTextRenderer(ibDataViewIconTextRenderer::GetDefaultType(), wxDATAVIEW_CELL_INERT),
		kGridCol1, FromDIP(178), wxAlignment::wxALIGN_LEFT));

	left->Add(m_available, wxSizerFlags(1).Expand());
	return left;
}

// ---- right: the rows, every cell edited where it stands, and the arrows that order them.
wxSizer* ibDialogChoiceParameters::BuildRowsPane()
{
	wxBoxSizer* right = new wxBoxSizer(wxVERTICAL);
	right->Add(new wxStaticText(this, wxID_ANY, _("Parameters:")), wxSizerFlags().Border(wxBOTTOM, FromDIP(2)));

	wxBoxSizer* rowsAndOrder = new wxBoxSizer(wxHORIZONTAL);
	m_rows = new ibDataViewCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxDV_ROW_LINES | wxDV_SINGLE);
	m_rowsModel = new ibQueryGridModel();

	m_rowsModel->SetReader([this](unsigned int row, unsigned int col) { return RowCellText(row, col); });
	m_rowsModel->SetWriter([this](unsigned int row, unsigned int col, const wxString& text) {
		return SetRowCellText(row, col, text);
	});

	m_rows->AssociateModel(m_rowsModel);

	// ⚠ THE COLUMNS ARE SIZED TO FIT THEIR PANE, and that is why the numbers look arbitrary. A grid
	// whose columns are wider than the space it was given draws a horizontal scrollbar the moment it
	// opens — before anybody has done anything — and a scrollbar on an untouched window reads as a
	// window that did not fit its own contents (Max, 2026-09-23).
	//
	// ⭐ BOTH HALVES OF A ROW DROP DOWN IN THE CELL: the parameter by name, the attribute by name. The
	// second column is titled with the object those attributes belong to, so the row reads as one
	// sentence — "Filter.Owner, taken from GoodsReceipt's Counterparty".
	m_rows->GetRootColumnGroup()->AppendColumn(new ibDataViewColumn(_("Name"),
		new ibDataViewChoiceRenderer(m_parameterWords, wxDATAVIEW_CELL_EDITABLE),
		kGridCol1, FromDIP(196), wxAlignment::wxALIGN_LEFT));
	m_rows->GetRootColumnGroup()->AppendColumn(new ibDataViewColumn(m_holder.IsEmpty()
		? _("Attribute") : wxString::Format(_("Attribute of %s"), m_holder),
		new ibDataViewChoiceRenderer(m_sourceWords, wxDATAVIEW_CELL_EDITABLE),
		kGridCol2, FromDIP(168), wxAlignment::wxALIGN_LEFT));
	m_rows->GetRootColumnGroup()->AppendColumn(new ibDataViewColumn(_("On change"),
		new ibDataViewChoiceRenderer(OnChangeWords(), wxDATAVIEW_CELL_EDITABLE),
		kGridCol3, FromDIP(104), wxAlignment::wxALIGN_LEFT));

	rowsAndOrder->Add(m_rows, wxSizerFlags(1).Expand());

	// ⭐ THE ORDER IS THE AUTHOR'S. It is the order the table reads in — a list whose rows cannot be
	// moved makes an author delete and re-add to say "this one first".
	//
	// ⚠ THE PICTURES COME FROM THE ART PROVIDER, not from arrow CHARACTERS. Those were written as
	// literals and came out as mojibake the moment the compiler read the file as the system codepage
	// (Max, 2026-09-23, screenshot) — a bitmap has no encoding to get wrong.
	wxBoxSizer* order = new wxBoxSizer(wxVERTICAL);
	m_up = new wxBitmapButton(this, wxID_UP, wxArtProvider::GetBitmapBundle(wxART_GO_UP, wxART_BUTTON));
	m_down = new wxBitmapButton(this, wxID_DOWN, wxArtProvider::GetBitmapBundle(wxART_GO_DOWN, wxART_BUTTON));
	order->Add(m_up, wxSizerFlags().Border(wxBOTTOM, FromDIP(4)));
	order->Add(m_down, wxSizerFlags());
	rowsAndOrder->Add(order, wxSizerFlags().Border(wxLEFT, FromDIP(4)));
	right->Add(rowsAndOrder, wxSizerFlags(1).Expand());
	return right;
}

// What a row's cell says — the rows grid's reader.
wxString ibDialogChoiceParameters::RowCellText(unsigned int row, unsigned int col) const
{
	if (row >= m_params.m_rows.size())
		return wxString();
	const ibChoiceParameterRowDescription& line = m_params.m_rows[row];

	if (col == kGridCol1) {
		// ⚠ A parameter the catalogue does not know is a row written against a field that has since
		// gone. It is SHOWN — as its id — rather than hidden: a row an author cannot see is a row
		// they cannot delete. A row whose parameter is not chosen YET shows nothing, and the cell
		// is where it is chosen.
		if (line.m_parameter == 0)
			return wxString();
		const ibOffered* parameter = ParameterOf(line.m_parameter);
		return parameter != nullptr ? kFilter + parameter->m_label
			: wxString::Format(_("Filter.<field %i is gone>"), (int)line.m_parameter);
	}

	if (col == kGridCol2) {
		const ibOffered* source = SourceOf(line.m_source);
		return source != nullptr ? source->m_label : wxString();
	}

	return OnChangeWords()[(size_t)line.m_onChange];
}

// ⭐ THE EDIT LANDS IN THE ROW, and nowhere else: the grid hands back the WORD that was chosen and
// the catalogue turns it into the thing it names. A word that names nothing refuses the edit, which
// leaves the old value standing rather than writing an empty one.
bool ibDialogChoiceParameters::SetRowCellText(unsigned int row, unsigned int col, const wxString& text)
{
	if (row >= m_params.m_rows.size())
		return false;
	ibChoiceParameterRowDescription& line = m_params.m_rows[row];

	if (col == kGridCol1) {
		const ibOffered* parameter = ParameterNamed(text);
		if (parameter == nullptr)
			return false;
		line.m_parameter = parameter->m_id;
		return true;
	}

	if (col == kGridCol2) {
		const ibOffered* source = SourceNamed(text);
		if (source == nullptr)
			return false;
		line.m_source = source->m_path;
		return true;
	}

	const wxArrayString words = OnChangeWords();
	const int picked = words.Index(text);
	if (picked == wxNOT_FOUND)
		return false;
	line.m_onChange = (ibChoiceParameterOnChange)picked;
	return true;
}

void ibDialogChoiceParameters::BindDragAndDrop()
{
#if wxUSE_DRAG_AND_DROP && wxUSE_UNICODE
	// ⭐⭐ DRAGGED, BECAUSE THAT IS WHAT A PERSON TRIES FIRST. Two drags, one drop: an attribute carried
	// from the left pane is ADDED where it lands; a row carried inside the right pane is MOVED there.
	// The payload says which of the two it is, because a drop that could not tell them apart would add
	// a row where it was asked to move one (Max, 2026-09-23: "drag and drop does not work").
	m_available->EnableDragSource(wxDF_UNICODETEXT);
	m_rows->EnableDragSource(wxDF_UNICODETEXT);
	m_rows->EnableDropTarget(wxDF_UNICODETEXT);

	m_available->Bind(wxEVT_DATAVIEW_ITEM_BEGIN_DRAG, [this](ibDataViewEvent& event) {
		const long line = (long)m_availableModel->GetRow(event.GetItem());
		if (line < 0 || (size_t)line >= m_availableSources.size()) {
			event.Veto();
			return;
		}
		event.SetDataObject(new wxTextDataObject(kDragAttribute + wxString::Format(wxT("%ld"), line)));
		event.SetDragFlags(wxDrag_CopyOnly);
	});

	m_rows->Bind(wxEVT_DATAVIEW_ITEM_BEGIN_DRAG, [this](ibDataViewEvent& event) {
		const long line = (long)m_rowsModel->GetRow(event.GetItem());
		if (line < 0 || (size_t)line >= m_params.m_rows.size()) {
			event.Veto();
			return;
		}
		event.SetDataObject(new wxTextDataObject(kDragRow + wxString::Format(wxT("%ld"), line)));
		event.SetDragFlags(wxDrag_AllowMove);
	});

	// ⚠ THE EFFECT IS LEFT AS PROPOSED, not forced. Forcing wxDragMove here refused every drop that
	// began as a COPY — and the attribute drag was started copy-only, so the pane it was aimed at simply
	// would not take it: the drag picture followed the cursor and nothing happened (Max, 2026-09-23).
	// The control reads the effect back out of this event (datavgen.cpp, OnDragOver), so saying nothing
	// is what "whatever the source allowed" looks like.
	m_rows->Bind(wxEVT_DATAVIEW_ITEM_DROP_POSSIBLE, [](ibDataViewEvent& event) {
		if (event.GetDataFormat() != wxDF_UNICODETEXT)
			event.Veto();
	});

	m_rows->Bind(wxEVT_DATAVIEW_ITEM_DROP, [this](ibDataViewEvent& event) {
		if (event.GetDataFormat() != wxDF_UNICODETEXT) {
			event.Veto();
			return;
		}

		wxTextDataObject carried;
		carried.SetData(event.GetDataSize(), event.GetDataBuffer());
		const wxString said = carried.GetText();

		// WHERE IT LANDED: on a row, or past the last one (an item that is not ok = the empty space).
		const ibDataViewItem onto = event.GetItem();
		const long at = onto.IsOk() ? (long)m_rowsModel->GetRow(onto) : (long)m_params.m_rows.size();

		if (said.StartsWith(kDragAttribute)) {
			long picked = 0;
			if (!said.Mid(kDragAttribute.length()).ToLong(&picked)
				|| picked < 0 || (size_t)picked >= m_availableSources.size())
				return;

			AddRow(m_availableSources[(size_t)picked], at);   // dropped or pressed, the same row and question
			return;
		}

		if (said.StartsWith(kDragRow)) {
			long from = 0;
			if (!said.Mid(kDragRow.length()).ToLong(&from)
				|| from < 0 || (size_t)from >= m_params.m_rows.size() || from == at)
				return;

			MoveRow(from, at > from ? at - 1 : at);   // the row it passed closed the gap
		}
	});
#endif // wxUSE_DRAG_AND_DROP && wxUSE_UNICODE
}

// ⭐⭐ AN ATTRIBUTE AND A PARAMETER THAT SHARE A NAME ARE THE SAME THING, so the row comes filled in.
// An author carrying `Organisation` over to a list narrowed by the target's `Organisation` is not
// making a decision — they are repeating one, and a window that makes them repeat it teaches them to
// click through it (Max, 2026-09-23: "and make it substitute them straight away when the names match").
//
// ⚠ A GUESS THAT IS ALWAYS VISIBLE AND ALWAYS EDITABLE. It lands in the cell where any other answer
// would, so an author who meant something else changes it exactly as they would have chosen it.
ibChoiceParameterRowDescription ibDialogChoiceParameters::NewRow(size_t source) const
{
	ibChoiceParameterRowDescription row;
	row.m_onChange = ibChoiceParameterOnChange::Clear;
	if (source >= m_sources.size())
		return row;

	row.m_source = m_sources[source].m_path;

	// ⭐ WHAT IS OBVIOUS IS FILLED IN; what is a real choice is left to the author — who cannot leave it
	// empty, because OK refuses a row with no name. Which of the two it is, is the backend's answer and
	// not a count read off the list here: a name that matched used to be thrown away whenever anything
	// else also fitted.
	if (m_property != nullptr) {
		std::vector<ibMetaID> fits;
		ibChoiceParametersForSource(m_property->GetPropertyObject(), m_sources[source].m_id,
			fits, &row.m_parameter);
	}

	return row;
}

// ⭐⭐ WHAT THIS ATTRIBUTE COULD FILL — asked of the backend, which has the types this window does not
// carry, and CACHED, because the left pane asks it for every attribute every time it refills.
const std::vector<ibMetaID>& ibDialogChoiceParameters::FitsOf(size_t source) const
{
	static const std::vector<ibMetaID> s_none;
	if (source >= m_sources.size())
		return s_none;

	auto found = m_fits.find(m_sources[source].m_id);
	if (found != m_fits.end())
		return found->second;

	std::vector<ibMetaID> fits;
	if (m_property != nullptr)
		ibChoiceParametersForSource(m_property->GetPropertyObject(), m_sources[source].m_id, fits);
	return m_fits.emplace(m_sources[source].m_id, std::move(fits)).first->second;
}

const ibDialogChoiceParameters::ibOffered* ibDialogChoiceParameters::ParameterOf(const ibMetaID& id) const
{
	for (const ibOffered& offered : m_parameters)
		if (offered.m_id == id)
			return &offered;
	return nullptr;
}

const ibDialogChoiceParameters::ibOffered* ibDialogChoiceParameters::ParameterNamed(const wxString& label) const
{
	wxString bare = label;
	if (bare.StartsWith(kFilter))
		bare = bare.Mid(kFilter.length());

	for (const ibOffered& offered : m_parameters)
		if (offered.m_label.IsSameAs(bare, false))
			return &offered;
	return nullptr;
}

const ibDialogChoiceParameters::ibOffered* ibDialogChoiceParameters::SourceOf(const ibSourceDescription& path) const
{
	for (const ibOffered& offered : m_sources)
		if (offered.m_path.m_listSource == path.m_listSource)
			return &offered;
	return nullptr;
}

const ibDialogChoiceParameters::ibOffered* ibDialogChoiceParameters::SourceNamed(const wxString& label) const
{
	for (const ibOffered& offered : m_sources)
		if (offered.m_label.IsSameAs(label, false))
			return &offered;
	return nullptr;
}

// ⭐ THE ATTRIBUTES NOT YET CARRIED OVER. One attribute supplies one row: an attribute already standing
// in the table is not offered again, exactly as the reference implementation leaves the used ones out.
void ibDialogChoiceParameters::FillAvailable()
{
	m_availableSources.clear();
	for (size_t idx = 0; idx < m_sources.size(); idx++) {
		// ⭐⭐ AND AN ATTRIBUTE THAT FILLS NOTHING IS NOT OFFERED. A parameter is `field of the target =
		// value of this attribute`, so an attribute no field of the target could hold would write a
		// condition that never matches — an empty list and nothing on screen saying why. Showing only
		// what fits also means the choice among what is left is a real one (Max, 2026-09-23: "show on
		// the left what suits us — then there is nothing to guess").
		if (FitsOf(idx).empty())
			continue;

		bool used = false;
		for (const ibChoiceParameterRowDescription& row : m_params.m_rows)
			used = used || row.m_source.m_listSource == m_sources[idx].m_path.m_listSource;
		if (!used)
			m_availableSources.push_back(idx);
	}
	m_availableModel->SetRowCount((unsigned int)m_availableSources.size());
}

void ibDialogChoiceParameters::FillRows()
{
	m_rowsModel->SetRowCount((unsigned int)m_params.m_rows.size());
}

long ibDialogChoiceParameters::SelectedLine() const
{
	const ibDataViewItem item = m_rows->GetSelection();
	return item.IsOk() ? (long)m_rowsModel->GetRow(item) : wxNOT_FOUND;
}

void ibDialogChoiceParameters::SelectLine(long line)
{
	if (line < 0 || (size_t)line >= m_params.m_rows.size())
		return;
	const ibDataViewItem item = m_rowsModel->GetItem((unsigned int)line);
	m_rows->Select(item);
	m_rows->EnsureVisible(item);
}

void ibDialogChoiceParameters::ShowSelection()
{
	const long line = SelectedLine();
	m_up->Enable(line > 0);
	m_down->Enable(line >= 0 && (size_t)line + 1 < m_params.m_rows.size());
}

// ⚠ A ROW MAY BE ADDED WITH ITS PARAMETER UNCHOSEN, and that is deliberate: the attribute is what the
// author carried over, the parameter is the next question, and refusing to add the row until both are
// answered would mean answering the second one in a list that does not ask it. A row with no parameter
// narrows nothing and is reported as unfilled at run time rather than silently passing every value.
void ibDialogChoiceParameters::AddRow(size_t available, long at)
{
	at = std::min<long>(std::max<long>(at, 0), (long)m_params.m_rows.size());
	m_params.m_rows.insert(m_params.m_rows.begin() + at, NewRow(available));

	FillAvailable();
	FillRows();
	SelectLine(at);
	ShowSelection();
	AskTheName(at);
}

void ibDialogChoiceParameters::MoveRow(long from, long to)
{
	if (from < 0 || (size_t)from >= m_params.m_rows.size())
		return;
	to = std::min<long>(std::max<long>(to, 0), (long)m_params.m_rows.size() - 1);
	if (to == from)
		return;

	const ibChoiceParameterRowDescription moved = m_params.m_rows[(size_t)from];
	m_params.m_rows.erase(m_params.m_rows.begin() + from);
	m_params.m_rows.insert(m_params.m_rows.begin() + to, moved);

	FillRows();
	SelectLine(to);
	ShowSelection();
}

void ibDialogChoiceParameters::OnAdd(wxCommandEvent&)
{
	const ibDataViewItem item = m_available->GetSelection();
	if (!item.IsOk())
		return;

	const long picked = (long)m_availableModel->GetRow(item);
	if (picked < 0 || (size_t)picked >= m_availableSources.size())
		return;

	AddRow(m_availableSources[(size_t)picked], (long)m_params.m_rows.size());
}

// ⭐⭐ AND WHERE THE ANSWER IS A REAL CHOICE, THE QUESTION IS ASKED — the Name cell opens on the row
// that was just carried over, with its candidates dropped down. A cell left blank and silent reads as a
// fault: a column of a section pointing at its own catalogue fits both `Ref` and `Parent`, which is two
// genuinely different filters and cannot be guessed — but "it did not fill in" and "it is asking you"
// look identical when nothing happens (Max, 2026-09-23: "empty again").
//
// Nothing is opened where the name was filled in: there is no question to ask.
void ibDialogChoiceParameters::AskTheName(long line)
{
	if (line < 0 || (size_t)line >= m_params.m_rows.size() || m_params.m_rows[(size_t)line].m_parameter != 0)
		return;

	if (ibDataViewColumn* name = m_rows->GetColumn(0)) {
		const ibDataViewItem item = m_rowsModel->GetItem((unsigned int)line);
		m_rows->CallAfter([this, item, name] { m_rows->EditItem(item, name); });
	}
}

void ibDialogChoiceParameters::OnRemove(wxCommandEvent&)
{
	const long line = SelectedLine();
	if (line < 0 || (size_t)line >= m_params.m_rows.size())
		return;

	m_params.m_rows.erase(m_params.m_rows.begin() + line);
	FillAvailable();
	FillRows();
	SelectLine(line < (long)m_params.m_rows.size() ? line : (long)m_params.m_rows.size() - 1);
	ShowSelection();
}

// The two of them are one act with a sign — the row swaps with the one it passes, and the SELECTION
// travels with the row, because what the author is holding is the row and not the line number.
void ibDialogChoiceParameters::OnMoveUp(wxCommandEvent&)
{
	const long line = SelectedLine();
	if (line > 0)
		MoveRow(line, line - 1);
}

void ibDialogChoiceParameters::OnMoveDown(wxCommandEvent&)
{
	const long line = SelectedLine();
	if (line >= 0)
		MoveRow(line, line + 1);
}

// ⭐⭐ A ROW WITHOUT A PARAMETER IS NOT A ROW, and OK says so rather than storing it. It would narrow
// nothing while sitting in the table looking like a setting — and a setting that does nothing is worse
// than a missing one, because nobody goes looking for it. The row is selected and the Name cell opened,
// so the refusal ends where the answer goes.
bool ibDialogChoiceParameters::Validate()
{
	for (size_t idx = 0; idx < m_params.m_rows.size(); idx++) {
		if (m_params.m_rows[idx].m_parameter != 0)
			continue;

		const ibOffered* source = SourceOf(m_params.m_rows[idx].m_source);
		wxMessageBox(wxString::Format(
			_("The row taking its value from '%s' has no parameter name.\n\n"
			  "Choose one in the Name column - it says WHICH field of the chosen object this narrows - "
			  "or remove the row."),
			source != nullptr ? source->m_label : wxString(_("this attribute"))),
			_("Choice parameter links"), wxOK | wxICON_EXCLAMATION, this);

		SelectLine((long)idx);
		ShowSelection();
		if (ibDataViewColumn* name = m_rows->GetColumn(0))
			m_rows->EditItem(m_rowsModel->GetItem((unsigned int)idx), name);
		return false;
	}

	return true;
}

// THE BUFFER IS WRITTEN BACK ONLY ON OK — Cancel really cancels, the shape the schedule editor beside
// this one uses. Rows left half-answered stay as they are: the author is mid-thought, and throwing
// their line away on OK would be the window deciding what they meant.
bool ibDialogChoiceParameters::ShowChoiceParametersDialog(ibPropertyChoiceParameters* property,
	ibChoiceParametersDescription& params)
{
	if (property == nullptr)
		return false;

	ibDialogChoiceParameters dialog(nullptr, property, params);
	if (dialog.ShowModal() != wxID_OK || dialog.GetParameters() == params)
		return false;

	params = dialog.GetParameters();
	return true;
}
