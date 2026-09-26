#include "choiceLink.h"

#include "backend/propertyManager/property/propertyChoiceLink.h"

#include "frontend/win/dlgs/queryConstructor/queryGridModel.h"   // the one grid model for a plain list

#include <wx/sizer.h>
#include <wx/statline.h>
#include <wx/artprov.h>

namespace {

// 🛑 A ROW WITHOUT A PICTURE DREW ITS NAME BLANK — the grid model hands an icon-text VALUE only when it
// has an icon and falls back to a plain string, which an icon-text renderer cannot read. So a picture
// is guaranteed rather than hoped for (the same note stands in the companion window).
wxIcon FieldPicture(const wxIcon& own, const wxIcon& ordinary)
{
	if (own.IsOk())
		return own;
	if (ordinary.IsOk())
		return ordinary;
	return wxArtProvider::GetIcon(wxART_NORMAL_FILE, wxART_MENU, wxSize(16, 16));
}

}

ibDialogChoiceLink::ibDialogChoiceLink(wxWindow* parent, ibPropertyChoiceLink* property,
	const ibChoiceTypeLinkDescription& link)
	: wxDialog(parent, wxID_ANY, _("Link by type"), wxDefaultPosition, wxSize(560, 430),
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
	, m_property(property), m_link(link)
{
	BuildCatalogue();
	BuildControls();
	ShowSelected();
}

// ⭐ ASKED ONCE, HELD. Both lists are the backend property's answer — which fields CARRY a type
// (ibChoiceLinkResolver::CanGovern), and which of this field's own types a link may decide — so the
// window offers exactly what the runtime can read, and the label, the picture and the reading of the
// current value come from one place.
void ibDialogChoiceLink::BuildCatalogue()
{
	if (m_property == nullptr)
		return;

	const auto keep = [](std::vector<ibCandidate>& into, const ibPropertyChoiceList& list, unsigned int idx) {
		ibCandidate candidate;
		candidate.m_id = (ibMetaID)list.GetId(idx);
		candidate.m_label = list.GetName(idx);
		const wxBitmap& picture = list.GetBitmap(idx);
		if (picture.IsOk())
			candidate.m_icon.CopyFromBitmap(picture);
		into.push_back(candidate);
	};

	// ⭐ "NOTHING GOVERNS IT" IS A CHOICE, so it stands in the list. A separate Clear button would be a
	// second way to say the same thing, and the one an author does not find is the one that is missing.
	ibCandidate none;
	none.m_label = _("<not set> - the field opens the list its own type declares");
	m_fields.push_back(none);

	ibPropertyChoiceList fields;
	m_property->GetValueList(fields);
	for (unsigned int idx = 0; idx < fields.GetCount(); idx++)
		keep(m_fields, fields, idx);

	ibPropertyChoiceList types;
	ibFieldReferenceTypes(m_property->GetPropertyObject(), types);
	for (unsigned int idx = 0; idx < types.GetCount(); idx++)
		keep(m_types, types, idx);

	// THE PICTURE AN ORDINARY FIELD CARRIES — taken from the list itself, so it is whatever the tree
	// draws today. A field that carries none is drawn with it rather than drawn as nothing.
	for (const ibCandidate& candidate : m_fields) {
		if (candidate.m_icon.IsOk()) { m_fieldIcon = candidate.m_icon; break; }
	}
}

void ibDialogChoiceLink::BuildControls()
{
	wxBoxSizer* outer = new wxBoxSizer(wxVERTICAL);

	// ⭐⭐ WHEN NOTHING CAN GOVERN IT, THAT IS THE WHOLE WINDOW — one paragraph, in place of the list AND
	// in place of the heading above it. The first cut showed both: a heading saying what a link is, an
	// empty grid, and a paragraph saying the same thing again underneath (Max, 2026-09-23). A window
	// that explains itself twice reads as a window that does not know what it is for.
	if (m_fields.size() <= 1) {
		wxStaticText* none = new wxStaticText(this, wxID_ANY,
			_("Nothing standing beside this field can say what a value in it may be.\n\n"
			  "A link takes that from the field it names: a characteristic says it through its chart, "
			  "a field holding a type description says it outright. In a tabular section the neighbours "
			  "are the other columns of the same row."));
		none->Wrap(FromDIP(500));
		outer->Add(none, wxSizerFlags(1).Expand().Border(wxALL, FromDIP(12)));
		outer->Add(new wxStaticLine(this), wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT, FromDIP(6)));
		outer->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), wxSizerFlags().Expand().Border(wxALL, FromDIP(6)));
		SetSizer(outer);
		return;
	}

	// ⚠ WRAPPED, not left to be cut off by the window's width — a sentence that ends in mid-air reads
	// as a fault in the window rather than as an explanation of it.
	wxStaticText* says = new wxStaticText(this, wxID_ANY,
		_("The field whose value decides the TYPE of this one."));
	says->Wrap(FromDIP(500));
	outer->Add(says, wxSizerFlags().Expand().Border(wxALL, FromDIP(6)));

	m_list = new ibDataViewCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxDV_ROW_LINES | wxDV_SINGLE | wxDV_NO_HEADER);
	m_model = new ibQueryGridModel();
	m_model->SetReader([this](unsigned int row, unsigned int) -> wxString {
		return row < m_fields.size() ? m_fields[row].m_label : wxString();
	});
	m_model->SetIconColumn(kGridCol1, FieldPicture(wxNullIcon, m_fieldIcon));
	m_model->SetIconReader([this](unsigned int row) -> wxIcon {
		return FieldPicture(row < m_fields.size() ? m_fields[row].m_icon : wxNullIcon, m_fieldIcon);
	});
	m_list->AssociateModel(m_model);
	m_list->GetRootColumnGroup()->AppendColumn(new ibDataViewColumn(_("Field"),
		new ibDataViewIconTextRenderer(ibDataViewIconTextRenderer::GetDefaultType(), wxDATAVIEW_CELL_INERT),
		kGridCol1, FromDIP(500), wxAlignment::wxALIGN_LEFT));
	m_model->SetRowCount((unsigned int)m_fields.size());

	outer->Add(m_list, wxSizerFlags(1).Expand().Border(wxLEFT | wxRIGHT, FromDIP(6)));

	// ⭐ WHAT THE SELECTED FIELD WOULD DECIDE, said under the list. Choosing the field that types this
	// one is a consequence an author cannot see from a name, and showing the consequence is the whole
	// reason this is a window and not a drop-down row.
	m_says = new wxStaticText(this, wxID_ANY, wxEmptyString);
	outer->Add(m_says, wxSizerFlags().Expand().Border(wxALL, FromDIP(6)));

	wxBoxSizer* governed = new wxBoxSizer(wxHORIZONTAL);
	m_governedLabel = new wxStaticText(this, wxID_ANY, _("Decides this field's type:"));
	governed->Add(m_governedLabel, wxSizerFlags().CenterVertical().Border(wxRIGHT, FromDIP(6)));
	m_governed = new wxChoice(this, wxID_ANY);
	governed->Add(m_governed, wxSizerFlags(1).Expand());
	outer->Add(governed, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(6)));

	outer->Add(new wxStaticLine(this), wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT, FromDIP(6)));
	outer->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), wxSizerFlags().Expand().Border(wxALL, FromDIP(6)));

	SetSizer(outer);

	// ⚠ ONLY WHERE THERE IS A QUESTION. A field holding one reference type has one answer, and asking
	// for it would be asking somebody to confirm the only thing that could be true; the description
	// keeps 0 and the engine takes that type (docs/private/choice-links.md § 6).
	const bool asked = m_types.size() > 1;
	m_governedLabel->Show(asked);
	m_governed->Show(asked);
	if (asked) {
		for (const ibCandidate& type : m_types)
			m_governed->Append(type.m_label);
	}

	// The row standing on what is set now — read from the buffer, so reopening shows what was chosen.
	const ibMetaID governing = m_link.m_source.GetLeaf();
	for (size_t idx = 0; idx < m_fields.size(); idx++) {
		if (m_fields[idx].m_id == governing) {
			m_list->Select(m_model->GetItem((unsigned int)idx));
			break;
		}
	}

	m_list->Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, [this](ibDataViewEvent&) {
		const ibDataViewItem item = m_list->GetSelection();
		const long line = item.IsOk() ? (long)m_model->GetRow(item) : wxNOT_FOUND;
		if (line < 0 || (size_t)line >= m_fields.size())
			return;

		// <not set> takes the link off AND the governed type with it: a type governed by nobody is a
		// leftover that reads as a setting.
		m_link.Clear();
		if (m_fields[(size_t)line].m_id != 0)
			m_link.m_source.AppendSource(m_fields[(size_t)line].m_id);

		ShowSelected();
	});

	m_governed->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
		const int picked = m_governed->GetSelection();
		// The row answers with the metaID of the type's own metaobject, and the description keeps the
		// CLASS id — the reference kind of that object. One is the thing, the other is the thing's type.
		m_link.m_governedType = (picked != wxNOT_FOUND && (size_t)picked < m_types.size())
			? reference_to_clsid(m_types[(size_t)picked].m_id) : 0;
	});
}

void ibDialogChoiceLink::ShowSelected()
{
	// The window that had nothing to offer built no list at all — it said so in words instead, and
	// there is no selection to report on.
	if (m_list == nullptr)
		return;

	const ibDataViewItem item = m_list->GetSelection();
	const long line = item.IsOk() ? (long)m_model->GetRow(item) : wxNOT_FOUND;
	const bool governed = line > 0;   // line 0 is <not set>

	m_says->SetLabel(governed
		? wxString::Format(_("The type of this field is then whatever '%s' declares."), m_fields[(size_t)line].m_label)
		: _("Nothing decides the type of this field."));

	m_governed->Enable(governed);
	m_governedLabel->Enable(governed);

	if (m_types.size() > 1) {
		int selected = wxNOT_FOUND;
		for (size_t idx = 0; idx < m_types.size(); idx++) {
			if (reference_to_clsid(m_types[idx].m_id) == m_link.m_governedType) {
				selected = (int)idx;
				break;
			}
		}
		m_governed->SetSelection(selected);
	}

	Layout();
}

bool ibDialogChoiceLink::ShowChoiceLinkDialog(ibPropertyChoiceLink* property, ibChoiceTypeLinkDescription& link)
{
	if (property == nullptr)
		return false;

	ibDialogChoiceLink dialog(nullptr, property, link);
	if (dialog.ShowModal() != wxID_OK || dialog.GetLink() == link)
		return false;

	link = dialog.GetLink();
	return true;
}
