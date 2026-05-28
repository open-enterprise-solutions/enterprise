#include "docViewAuditLog.h"

#include "backend/appData.h"
#include "backend/logger/logger.h"
#include "backend/metaCollection/partial/reference/reference.h"
#include "backend/metadataConfiguration.h"
#include "backend/picturePredefined.h"

#include "frontend/visualView/ctrl/frame.h"
#include "frontend/win/dlgs/userItem.h"
#include "frontend/win/theme/luna_toolbarart.h"

#include <algorithm>

#include <wx/aui/auibar.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/datectrl.h>
#include <wx/datetime.h>
#include <wx/itemattr.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/timer.h>
#include <wx/variant.h>

namespace {

const wxChar* LevelTag(int lvl)
{
	switch (lvl) {
	case 0: return wxT("INFO");
	case 1: return wxT("WARN");
	case 2: return wxT("ERROR");
	case 3: return wxT("AUDIT");
	}
	return wxT("?");
}

// Source dropdown values mirror the Audit-source taxonomy enforced by
// producers. Empty entry = "any". Order: most relevant for admin first.
const wxChar* const kSources[] = {
	wxT(""), wxT("auth"), wxT("session"), wxT("record"),
	wxT("document"), wxT("metadata"), nullptr
};

struct LevelOption { const wxChar* label; int minLevel; };
const LevelOption kLevels[] = {
	{ wxT("All"),       -1 },
	{ wxT("Info+"),      0 },
	{ wxT("Warn+"),      1 },
	{ wxT("Error+"),     2 },
	{ wxT("Audit only"), 3 },
};

enum {
	wxID_AUDIT_TOOL_APPLY = wxID_HIGHEST + 1,
	wxID_AUDIT_TOOL_CLEAR,
	wxID_AUDIT_TOOL_REFRESH,
	wxID_AUDIT_TOOL_OPEN_REF,
};

}   // namespace

// ============================================================
//   ibAuditLogRowObject
// ============================================================

bool ibAuditLogRowObject::IsEqualTo(const ibDataViewObject& other) const
{
	// Same model + same loaded index → same row. Different models or
	// different indexes — not equal. Used by ibDataViewItem::operator==
	// so selection survives re-fetch when the underlying row is still
	// at the same index.
	auto* o = dynamic_cast<const ibAuditLogRowObject*>(&other);
	return o != nullptr && o->m_model == m_model && o->m_rowIndex == m_rowIndex;
}

// ============================================================
//   ibAuditLogModel
// ============================================================

void ibAuditLogModel::SetFilter(const ibLogFilter& f)
{
	m_baseFilter = f;
	// offset / limit are computed per page in LoadPage; never carry user
	// values through. Defensive — caller shouldn't set them but if they
	// do we'd silently double-apply the limit.
	m_baseFilter.offset = 0;
	m_baseFilter.limit  = 0;
}

const ibLogRow* ibAuditLogModel::RowAt(int index) const
{
	if (index < 0 || index >= static_cast<int>(m_loadedRows.size()))
		return nullptr;
	return &m_loadedRows[index];
}

const ibLogRow* ibAuditLogModel::RowAt(const ibDataViewItem& item) const
{
	auto* obj = dynamic_cast<ibAuditLogRowObject*>(item.GetID());
	return obj != nullptr ? RowAt(obj->GetRowIndex()) : nullptr;
}

void ibAuditLogModel::LoadPage(int offset, int count) const
{
	if (m_reader == nullptr) return;
	if (count <= 0) count = kPageSize;

	ibLogFilter f = m_baseFilter;
	f.offset = static_cast<std::size_t>(offset);
	f.limit  = static_cast<std::size_t>(count);

	std::vector<ibLogRow> page = m_reader->Query(f);
	for (auto& row : page) {
		const int idx = static_cast<int>(m_loadedRows.size());
		m_loadedRows.push_back(std::move(row));
		m_rowObjects.emplace_back(new ibAuditLogRowObject(
			const_cast<ibAuditLogModel*>(this), idx));
	}
}

unsigned int ibAuditLogModel::GetFirstFetch(
	const ibDataViewItem& /*parent*/,
	const ibDataViewItem& /*anchor*/,
	int count,
	ibDataViewItemArray& out) const
{
	// Wipe loaded data — first fetch implies a fresh window from the
	// top. Reload / filter change trips Cleared() → control re-calls
	// GetFirstFetch which re-enters here.
	m_loadedRows.clear();
	m_rowObjects.clear();

	if (count <= 0) count = kPageSize;
	LoadPage(0, count);

	for (auto& obj : m_rowObjects)
		out.Add(ibDataViewItem(obj.get()));
	return static_cast<unsigned int>(m_rowObjects.size());
}

unsigned int ibAuditLogModel::GetNextFetch(
	const ibDataViewItem& /*parent*/,
	const ibDataViewItem& /*anchor*/,
	int count,
	ibDataViewItemArray& out) const
{
	// Append a page after the last loaded row. The control drives this
	// when the user scrolls near the bottom of its prefetched window.
	if (count <= 0) count = kPageSize;
	const int startIdx = static_cast<int>(m_loadedRows.size());
	LoadPage(startIdx, count);
	const int endIdx = static_cast<int>(m_loadedRows.size());
	for (int i = startIdx; i < endIdx; ++i)
		out.Add(ibDataViewItem(m_rowObjects[i].get()));
	return static_cast<unsigned int>(endIdx - startIdx);
}

unsigned int ibAuditLogModel::GetPrevFetch(
	const ibDataViewItem& /*parent*/,
	const ibDataViewItem& anchor,
	int count,
	ibDataViewItemArray& out) const
{
	// Backward scroll: control's deque dropped some earlier-loaded rows
	// from its visible window. We never evict from m_loadedRows /
	// m_rowObjects, so the rows are still alive — just re-hand the
	// `count` items immediately before the anchor's index.
	if (count <= 0) count = kPageSize;
	auto* anchorObj = anchor.IsOk()
		? dynamic_cast<ibAuditLogRowObject*>(anchor.GetID())
		: nullptr;
	if (anchorObj == nullptr) return 0;

	const int anchorIdx = anchorObj->GetRowIndex();
	if (anchorIdx <= 0) return 0;
	const int startIdx = std::max(0, anchorIdx - count);
	for (int i = startIdx; i < anchorIdx; ++i)
		out.Add(ibDataViewItem(m_rowObjects[i].get()));
	return static_cast<unsigned int>(anchorIdx - startIdx);
}

void ibAuditLogModel::GetValue(wxVariant& val,
                                const ibDataViewItem& item,
                                unsigned int col) const
{
	auto* obj = dynamic_cast<ibAuditLogRowObject*>(item.GetID());
	if (obj == nullptr) return;
	const ibLogRow* r = RowAt(obj->GetRowIndex());
	if (r == nullptr) return;
	switch (col) {
	case kColTime: {
		const wxDateTime t((wxLongLong)r->ts_ms);
		val = t.Format(wxT("%Y-%m-%d %H:%M:%S"));
		break;
	}
	case kColLevel:   val = wxString(LevelTag(r->level)); break;
	case kColUser:    val = r->user_name; break;
	case kColSource:  val = r->source;    break;
	case kColEvent:   val = r->event_type; break;
	case kColMessage: val = r->message;   break;
	case kColRef:     val = r->ref_guid.IsEmpty()
	                       ? wxString()
	                       : r->ref_guid.Left(8) + wxT("…");
	                  break;
	}
}

// ============================================================
//   ibAuditLogDocument
// ============================================================

wxIMPLEMENT_DYNAMIC_CLASS(ibAuditLogDocument, ibMetaDocument);

ibAuditLogDocument::ibAuditLogDocument() : ibMetaDocument()
{
	// Top-level tool tab — not a child of another document.
	m_childDoc = false;

	if (appData != nullptr && appData->GetLogger() != nullptr) {
		m_reader = std::make_unique<ibLoggerReader>(
			appData->GetLogger()->GetLogDir());
	}
}

ibMetaView* ibAuditLogDocument::DoCreateView()
{
	return new ibAuditLogView();
}

// ============================================================
//   ibAuditLogView
// ============================================================

wxIMPLEMENT_DYNAMIC_CLASS(ibAuditLogView, ibMetaView);

wxBEGIN_EVENT_TABLE(ibAuditLogView, ibMetaView)
wxEND_EVENT_TABLE()

bool ibAuditLogView::OnCreate(ibMetaDocument* doc, long flags)
{
	m_auditDoc = dynamic_cast<ibAuditLogDocument*>(doc);

	m_model.reset(new ibAuditLogModel());
	if (m_auditDoc != nullptr)
		m_model->SetReader(m_auditDoc->GetReader());

	m_tailTimer = std::make_shared<wxTimer>();
	m_tailTimer->Bind(wxEVT_TIMER, &ibAuditLogView::OnTimer, this);

	BuildLayout(m_viewFrame);

	// Initial fetch happens via the dataview ctrl driving GetFirstFetch
	// off AssociateModel inside BuildLayout — no extra trigger needed.
	return ibMetaView::OnCreate(doc, flags);
}

void ibAuditLogView::BuildLayout(wxWindow* parent)
{
	wxBoxSizer* main = new wxBoxSizer(wxVERTICAL);

	// ----- Filter strip -----
	wxBoxSizer* filt = new wxBoxSizer(wxHORIZONTAL);

	filt->Add(new wxStaticText(parent, wxID_ANY, _("From:")),
		0, wxALIGN_CENTER_VERTICAL | wxALL, parent->FromDIP(3));
	wxDateTime defaultFrom = wxDateTime::Today();
	defaultFrom.Subtract(wxDateSpan::Days(7));
	m_fromDate = new wxDatePickerCtrl(parent, wxID_ANY, defaultFrom,
		wxDefaultPosition, wxDefaultSize, wxDP_DROPDOWN | wxDP_SHOWCENTURY);
	filt->Add(m_fromDate, 0, wxALL, parent->FromDIP(3));

	filt->Add(new wxStaticText(parent, wxID_ANY, _("To:")),
		0, wxALIGN_CENTER_VERTICAL | wxALL, parent->FromDIP(3));
	m_toDate = new wxDatePickerCtrl(parent, wxID_ANY, wxDateTime::Today(),
		wxDefaultPosition, wxDefaultSize, wxDP_DROPDOWN | wxDP_SHOWCENTURY);
	filt->Add(m_toDate, 0, wxALL, parent->FromDIP(3));

	filt->Add(new wxStaticText(parent, wxID_ANY, _("Level:")),
		0, wxALIGN_CENTER_VERTICAL | wxALL, parent->FromDIP(3));
	m_levelChoice = new wxChoice(parent, wxID_ANY);
	for (const auto& l : kLevels) m_levelChoice->Append(l.label);
	m_levelChoice->SetSelection(4);                              // Audit only
	filt->Add(m_levelChoice, 0, wxALL, parent->FromDIP(3));

	filt->Add(new wxStaticText(parent, wxID_ANY, _("Source:")),
		0, wxALIGN_CENTER_VERTICAL | wxALL, parent->FromDIP(3));
	m_sourceChoice = new wxChoice(parent, wxID_ANY);
	for (int i = 0; kSources[i] != nullptr; ++i) {
		m_sourceChoice->Append(kSources[i][0] == 0 ? _("any") : wxString(kSources[i]));
	}
	m_sourceChoice->SetSelection(0);
	filt->Add(m_sourceChoice, 0, wxALL, parent->FromDIP(3));

	filt->Add(new wxStaticText(parent, wxID_ANY, _("User:")),
		0, wxALIGN_CENTER_VERTICAL | wxALL, parent->FromDIP(3));
	m_userText = new wxTextCtrl(parent, wxID_ANY, wxEmptyString,
		wxDefaultPosition, wxSize(parent->FromDIP(120), -1));
	filt->Add(m_userText, 0, wxALL, parent->FromDIP(3));

	filt->Add(new wxStaticText(parent, wxID_ANY, _("Search:")),
		0, wxALIGN_CENTER_VERTICAL | wxALL, parent->FromDIP(3));
	m_searchText = new wxTextCtrl(parent, wxID_ANY, wxEmptyString,
		wxDefaultPosition, wxSize(parent->FromDIP(160), -1));
	filt->Add(m_searchText, 1, wxALL | wxEXPAND, parent->FromDIP(3));

	main->Add(filt, 0, wxEXPAND);

	// ----- AuiToolBar (Luna theme) — mirrors ibDialogUserList -----
	m_toolbarMain = new wxAuiToolBar(parent, wxID_ANY, wxDefaultPosition,
		wxDefaultSize, wxAUI_TB_HORZ_TEXT);
	m_toolbarMain->SetArtProvider(new wxAuiLunaToolBarArt());

	m_toolbarMain->AddTool(wxID_AUDIT_TOOL_APPLY,    _("Apply"),
		ibBackendPicture::GetPicture(g_picFilterSetCLSID));
	m_toolbarMain->AddTool(wxID_AUDIT_TOOL_CLEAR,    _("Clear"),
		ibBackendPicture::GetPicture(g_picFilterClearCLSID));
	m_toolbarMain->AddTool(wxID_AUDIT_TOOL_REFRESH,  _("Refresh"),
		ibBackendPicture::GetPicture(g_picUpdateFormCLSID));
	m_toolbarMain->AddSeparator();
	m_toolbarMain->AddTool(wxID_AUDIT_TOOL_OPEN_REF, _("Open object"),
		ibBackendPicture::GetPicture(g_picSelectCLSID));

	// Live-refresh checkbox sits on the toolbar's right edge.
	m_tailCheck = new wxCheckBox(m_toolbarMain, wxID_ANY, _("Live refresh (3 s)"));
	m_tailCheck->Bind(wxEVT_CHECKBOX, &ibAuditLogView::OnTailToggle, this);
	m_toolbarMain->AddStretchSpacer(1);
	m_toolbarMain->AddControl(m_tailCheck);

	m_toolbarMain->SetForegroundColour(wxDefaultStypeFGColour);
	m_toolbarMain->SetBackgroundColour(wxDefaultStypeBGColour);
	m_toolbarMain->Realize();
	m_toolbarMain->Bind(wxEVT_MENU, &ibAuditLogView::OnCommandMenu, this);

	main->Add(m_toolbarMain, 0, wxEXPAND);

	// ----- DataView -----
	m_dataEditor = new ibDataViewCtrl(parent, wxID_ANY,
		wxDefaultPosition, wxDefaultSize, 0);
	m_dataEditor->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED,
		&ibAuditLogView::OnItemActivated, this);
	m_dataEditor->Bind(wxEVT_MENU, &ibAuditLogView::OnCommandMenu, this);

	m_dataEditor->AppendColumn(new ibDataViewColumn(_("Time"),
		new ibDataViewTextRenderer(wxT("string"), wxDATAVIEW_CELL_INERT),
		ibAuditLogModel::kColTime, parent->FromDIP(140), wxALIGN_LEFT, wxDATAVIEW_COL_SORTABLE));
	m_dataEditor->AppendColumn(new ibDataViewColumn(_("Level"),
		new ibDataViewTextRenderer(wxT("string"), wxDATAVIEW_CELL_INERT),
		ibAuditLogModel::kColLevel, parent->FromDIP(60), wxALIGN_LEFT, wxDATAVIEW_COL_SORTABLE));
	m_dataEditor->AppendColumn(new ibDataViewColumn(_("User"),
		new ibDataViewTextRenderer(wxT("string"), wxDATAVIEW_CELL_INERT),
		ibAuditLogModel::kColUser, parent->FromDIP(120), wxALIGN_LEFT, wxDATAVIEW_COL_SORTABLE));
	m_dataEditor->AppendColumn(new ibDataViewColumn(_("Source"),
		new ibDataViewTextRenderer(wxT("string"), wxDATAVIEW_CELL_INERT),
		ibAuditLogModel::kColSource, parent->FromDIP(80), wxALIGN_LEFT, wxDATAVIEW_COL_SORTABLE));
	m_dataEditor->AppendColumn(new ibDataViewColumn(_("Event"),
		new ibDataViewTextRenderer(wxT("string"), wxDATAVIEW_CELL_INERT),
		ibAuditLogModel::kColEvent, parent->FromDIP(100), wxALIGN_LEFT, wxDATAVIEW_COL_SORTABLE));
	m_dataEditor->AppendColumn(new ibDataViewColumn(_("Message"),
		new ibDataViewTextRenderer(wxT("string"), wxDATAVIEW_CELL_INERT),
		ibAuditLogModel::kColMessage, parent->FromDIP(320), wxALIGN_LEFT, 0));
	m_dataEditor->AppendColumn(new ibDataViewColumn(_("Ref"),
		new ibDataViewTextRenderer(wxT("string"), wxDATAVIEW_CELL_INERT),
		ibAuditLogModel::kColRef, parent->FromDIP(80), wxALIGN_LEFT, 0));

	m_dataEditor->SetForegroundColour(wxDefaultStypeFGColour);
	wxItemAttr attr(wxDefaultStypeFGColour, wxDefaultStypeBGColour,
		m_dataEditor->GetFont());
	m_dataEditor->SetHeaderAttr(attr);

	// Initial filter pulled from the strip defaults (Audit-only, last 7d).
	m_model->SetFilter(ReadFilter());
	m_dataEditor->AssociateModel(m_model.get());

	main->Add(m_dataEditor, 1, wxEXPAND | wxALL, parent->FromDIP(3));

	parent->SetSizer(main);
	parent->Layout();
}

ibLogFilter ibAuditLogView::ReadFilter() const
{
	ibLogFilter f;

	if (m_fromDate->GetValue().IsValid()) {
		wxDateTime from = m_fromDate->GetValue();
		from.ResetTime();
		f.from_ms = from.GetValue().GetValue();
	}
	if (m_toDate->GetValue().IsValid()) {
		wxDateTime to = m_toDate->GetValue();
		to.ResetTime();
		to.Add(wxDateSpan::Day());                                  // inclusive day
		f.to_ms = to.GetValue().GetValue();
	}

	const int lvlSel = m_levelChoice->GetSelection();
	if (lvlSel >= 0 && lvlSel < static_cast<int>(sizeof(kLevels) / sizeof(kLevels[0])))
		f.min_level = kLevels[lvlSel].minLevel;

	const int srcSel = m_sourceChoice->GetSelection();
	if (srcSel > 0 && kSources[srcSel] != nullptr)
		f.source = kSources[srcSel];

	f.user_name = m_userText->GetValue();
	f.search    = m_searchText->GetValue();
	return f;
}

void ibAuditLogView::Reload()
{
	if (!m_model) return;
	m_model->SetFilter(ReadFilter());
	// Cleared() notifies the dataview to wipe its window and re-fetch
	// page 0 via GetFirstFetch. The model's clear-and-load logic lives
	// inside GetFirstFetch itself.
	m_model->Cleared();
}


void ibAuditLogView::OnCommandMenu(wxCommandEvent& event)
{
	switch (event.GetId()) {
	case wxID_AUDIT_TOOL_APPLY:
	case wxID_AUDIT_TOOL_REFRESH:
		Reload();
		break;

	case wxID_AUDIT_TOOL_CLEAR: {
		wxDateTime from = wxDateTime::Today();
		from.Subtract(wxDateSpan::Days(7));
		m_fromDate->SetValue(from);
		m_toDate->SetValue(wxDateTime::Today());
		m_levelChoice->SetSelection(4);
		m_sourceChoice->SetSelection(0);
		m_userText->Clear();
		m_searchText->Clear();
		Reload();
		break;
	}

	case wxID_AUDIT_TOOL_OPEN_REF: {
		if (!m_dataEditor || !m_model) break;
		ibDataViewItem sel = m_dataEditor->GetSelection();
		if (!sel.IsOk()) break;
		const ibLogRow* r = m_model->RowAt(sel);
		if (r == nullptr || r->ref_guid.IsEmpty()) break;

		const ibGuid guid(r->ref_guid);
		if (r->ref_meta_id == 0) {
			// Convention from Phase 2g — system-table sys_user row.
			// Open the User Admin dialog keyed by guid instead of
			// going through metadata (sys_user is not a metaobject).
			ibDialogUserItem dlg(m_viewFrame, wxID_ANY);
			if (dlg.ReadUserData(guid)) dlg.ShowModal();
		}
		else if (activeMetaData != nullptr) {
			// Metadata-backed object — Catalog / Document / ChartOf*.
			ibValueReferenceDataObject* refVal =
				ibValueReferenceDataObject::Create(
					activeMetaData, r->ref_meta_id, guid);
			if (refVal != nullptr) {
				refVal->ShowValue();
			}
		}
		break;
	}
	}
	event.Skip();
}

void ibAuditLogView::OnItemActivated(ibDataViewEvent& event)
{
	wxCommandEvent fake(wxEVT_MENU, wxID_AUDIT_TOOL_OPEN_REF);
	OnCommandMenu(fake);
	event.Skip();
}

void ibAuditLogView::OnTailToggle(wxCommandEvent&)
{
	if (m_tailCheck == nullptr || !m_tailTimer) return;
	if (m_tailCheck->IsChecked()) m_tailTimer->Start(3000);
	else                          m_tailTimer->Stop();
}

void ibAuditLogView::OnTimer(wxTimerEvent&)
{
	Reload();
}

void ibAuditLogView::OnUpdate(wxView* /*sender*/, wxObject* /*hint*/)
{
	// UpdateAllViews fires on doc mutation. The journal is read-only;
	// any external refresh request just re-runs the query.
	Reload();
}

void ibAuditLogView::OnDraw(wxDC* /*dc*/)
{
	// Nothing to do — dataview / toolbar / inputs draw themselves.
}

bool ibAuditLogView::OnClose(bool deleteWindow)
{
	if (m_tailTimer && m_tailTimer->IsRunning()) m_tailTimer->Stop();

	if (deleteWindow) {
		if (m_viewFrame != nullptr) {
			m_viewFrame->Destroy();
			SetFrame(nullptr);
		}
	}

	return ibMetaView::OnClose(deleteWindow);
}
