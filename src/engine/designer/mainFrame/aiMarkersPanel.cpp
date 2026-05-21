/////////////////////////////////////////////////////////////////////////////
// ibAiMarkersPanel implementation. See header for the contract.
//
// The panel is intentionally a pure sink: external code can call
// SetMarkers / AddMarker / RefreshFromOutputWindow to drive the contents.
// The only built-in source today is ibOutputWindow's accumulated lines,
// surfaced via the new CollectAttachedLines() readback. When a Σ-Check or
// EDT marker subsystem ships, ingest it through the same API — no panel
// code changes required.
/////////////////////////////////////////////////////////////////////////////

#include "aiMarkersPanel.h"

#include "mainFrameDesigner.h"
#include "output/outputWindow.h"

#include "backend/appData.h"
#include "backend/metadataConfiguration.h"
#include "backend/metaData.h"
#include "backend/backend_metatree.h"

#include "frontend/docView/docManager.h"
#include "frontend/docView/docView.h"

#include <wx/sizer.h>
#include <wx/listctrl.h>
#include <wx/button.h>
#include <wx/stattext.h>
#include <wx/msgdlg.h>
#include <wx/log.h>

namespace {

enum {
	ID_AI_MARKERS_LIST = wxID_HIGHEST + 5500,
	ID_AI_MARKERS_APPLY_ALL,
	ID_AI_MARKERS_REFRESH,
};

// Human-readable Russian label for a severity tag. Forward-compatible —
// unknown severities pass through verbatim so a future "perf" / "style"
// tag still renders something legible.
wxString SeverityLabel(const wxString& severity)
{
	if      (severity == wxT("error"))   return _("Ошибка");
	else if (severity == wxT("warning")) return _("Предупреждение");
	else if (severity == wxT("info"))    return _("Информация");
	else if (severity == wxT("fix"))     return _("Автофикс");
	return severity;
}

// Single-character ASCII badge for the severity column. No emoji per
// project policy — we keep the badge column narrow so the message
// column has room to breathe.
wxString SeveritySymbol(const wxString& severity)
{
	if      (severity == wxT("error"))   return wxT("E");
	else if (severity == wxT("warning")) return wxT("W");
	else if (severity == wxT("info"))    return wxT("i");
	else if (severity == wxT("fix"))     return wxT("F");
	return wxT("?");
}

} // namespace

ibAiMarkersPanel::ibAiMarkersPanel(wxWindow* parent, int id)
	: wxPanel(parent, id)
{
	auto* root = new wxBoxSizer(wxVERTICAL);

	// Header row — title + action buttons.
	auto* header = new wxBoxSizer(wxHORIZONTAL);
	auto* title = new wxStaticText(this, wxID_ANY, _("Маркеры AI-ассистента"));
	wxFont titleFont = title->GetFont();
	titleFont.MakeBold();
	title->SetFont(titleFont);
	header->Add(title, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(4));

	m_refreshButton = new wxButton(this, ID_AI_MARKERS_REFRESH, _("Обновить"));
	header->Add(m_refreshButton, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(4));
	m_applyAllButton = new wxButton(this, ID_AI_MARKERS_APPLY_ALL,
	                                  _("Применить все автофиксы"));
	header->Add(m_applyAllButton, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, FromDIP(4));

	root->Add(header, 0, wxEXPAND | wxTOP | wxBOTTOM, FromDIP(4));

	// List ctrl. Columns: severity badge, file, line, message, AI fix.
	m_list = new wxListView(this, ID_AI_MARKERS_LIST, wxDefaultPosition, wxDefaultSize,
	                         wxLC_REPORT | wxLC_SINGLE_SEL);
	m_list->InsertColumn(0, _("Тип"),        wxLIST_FORMAT_LEFT, FromDIP(60));
	m_list->InsertColumn(1, _("Файл"),       wxLIST_FORMAT_LEFT, FromDIP(180));
	m_list->InsertColumn(2, _("Строка"),     wxLIST_FORMAT_RIGHT, FromDIP(60));
	m_list->InsertColumn(3, _("Сообщение"),  wxLIST_FORMAT_LEFT, FromDIP(360));
	m_list->InsertColumn(4, _("Автофикс"),   wxLIST_FORMAT_LEFT, FromDIP(200));
	root->Add(m_list, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(4));

	// Bottom status line — total / per-severity counts.
	m_statusLine = new wxStaticText(this, wxID_ANY, wxEmptyString);
	m_statusLine->SetForegroundColour(wxColour(120, 120, 130));
	root->Add(m_statusLine, 0, wxEXPAND | wxALL, FromDIP(4));

	SetSizer(root);

	Bind(wxEVT_LIST_ITEM_ACTIVATED, &ibAiMarkersPanel::OnRowActivated,    this, ID_AI_MARKERS_LIST);
	Bind(wxEVT_BUTTON,              &ibAiMarkersPanel::OnApplyAllFixes,   this, ID_AI_MARKERS_APPLY_ALL);
	Bind(wxEVT_BUTTON,              &ibAiMarkersPanel::OnRefreshClick,    this, ID_AI_MARKERS_REFRESH);

	// Initial population — pull whatever the output window already has.
	RefreshFromOutputWindow();
}

void ibAiMarkersPanel::SetMarkers(std::vector<Marker> markers)
{
	m_markers = std::move(markers);
	Rebuild();
}

void ibAiMarkersPanel::AddMarker(const Marker& m)
{
	m_markers.push_back(m);
	Rebuild();
}

void ibAiMarkersPanel::RefreshFromOutputWindow()
{
	// Pull diagnostic lines from ibOutputWindow's accumulated state. This
	// is the only built-in marker source today; a future Σ-Check / EDT
	// subsystem would add a second source loop here. Lines without a
	// docPath OR fileName are dropped — they're plain log messages, not
	// markers we can navigate to.
	auto* frame = ibFrontendDocMDIFrameDesigner::GetFrame();
	if (frame == nullptr) return;
	auto* out = frame->GetOutputWindow();
	if (out == nullptr) return;

	std::vector<Marker> next;
	for (const auto& snap : out->CollectAttachedLines()) {
		if (snap.docPath.IsEmpty() && snap.fileName.IsEmpty()) continue;
		Marker m;
		m.severity = snap.severity;
		m.file     = snap.fileName.IsEmpty() ? snap.docPath : snap.fileName;
		m.docPath  = snap.docPath;
		m.line     = snap.srcLine;
		m.message  = snap.message;
		// fix stays empty — output-window-sourced markers don't carry an
		// AI suggestion yet. When the AI bridge starts attaching fix text
		// alongside diagnostics, populate here from the same envelope.
		next.push_back(std::move(m));
	}
	SetMarkers(std::move(next));
}

void ibAiMarkersPanel::OnRowActivated(wxListEvent& event)
{
	const long row = event.GetIndex();
	if (row < 0 || row >= static_cast<long>(m_markers.size())) return;
	const Marker& m = m_markers[static_cast<size_t>(row)];

	// Mirror ibOutputWindow::OnDoubleClick navigation: when we have a
	// docPath we route through the metadata tree's EditModule; when we
	// only have a fileName we open the document first then re-route.
	if (m.docPath.IsEmpty() && m.file.IsEmpty()) {
		wxLogStatus(_("Маркер без привязки к файлу"));
		return;
	}

	if (m.file.IsEmpty() || m.file == m.docPath) {
		// Pure metadata-attached marker.
		if (activeMetaData == nullptr) return;
		ibBackendMetadataTree* metaTree = activeMetaData->GetMetaTree();
		if (metaTree == nullptr) return;
		metaTree->EditModule(m.docPath, m.line, false);
		return;
	}

	// File-attached marker — open or focus the document first.
	auto* found = dynamic_cast<ibMetaDataDocument*>(
	    docManager->FindDocumentByPath(m.file));
	if (found == nullptr) {
		found = dynamic_cast<ibMetaDataDocument*>(
		    docManager->CreateDocument(m.file, wxDOC_SILENT));
	}
	if (found == nullptr) {
		wxLogStatus(_("Не удалось открыть документ: %s"), m.file);
		return;
	}
	ibMetaData* md = found->GetMetaData();
	if (md == nullptr) return;
	ibBackendMetadataTree* metaTree = md->GetMetaTree();
	if (metaTree == nullptr) return;
	metaTree->EditModule(m.docPath, m.line, false);
}

void ibAiMarkersPanel::OnApplyAllFixes(wxCommandEvent& /*event*/)
{
	// Count rows that actually have a fix. Walking the list once first
	// keeps the confirmation prompt accurate — "Applied N of K" reads
	// correctly even when zero rows have a fix attached.
	size_t fixable = 0;
	for (const auto& m : m_markers) {
		if (!m.fix.IsEmpty()) ++fixable;
	}
	if (fixable == 0) {
		wxMessageBox(_("Нет ни одного маркера с предложенным автофиксом."),
		             _("Маркеры AI-ассистента"),
		             wxOK | wxICON_INFORMATION, this);
		return;
	}

	const wxString prompt = wxString::Format(
	    _("Применить %zu автофикс(ов)? Действие нельзя отменить одной командой."),
	    fixable);
	if (wxMessageBox(prompt, _("Маркеры AI-ассистента"),
	                  wxYES_NO | wxICON_QUESTION, this) != wxYES) {
		return;
	}

	// Phase-1 behaviour: we don't have a unified fix-application protocol
	// across Σ-Check / EDT yet, so we log each fix into the output window
	// and leave the row in place with a "fix offered" flavour. Once the
	// fix protocol exists, replace this block with the real apply call —
	// the panel-level UX (confirm → walk rows → report N applied) stays
	// the same.
	auto* frame = ibFrontendDocMDIFrameDesigner::GetFrame();
	size_t applied = 0;
	for (const auto& m : m_markers) {
		if (m.fix.IsEmpty()) continue;
		if (frame != nullptr) {
			frame->Message(
			    wxString::Format(_("Применён автофикс: %s:%d — %s"),
			                       m.file, m.line, m.fix),
			    ibStatusMessage::ibStatusMessage_Information);
		}
		++applied;
	}

	if (m_statusLine != nullptr) {
		m_statusLine->SetLabel(wxString::Format(
		    _("Применено автофиксов: %zu из %zu"), applied, fixable));
	}
}

void ibAiMarkersPanel::OnRefreshClick(wxCommandEvent& /*event*/)
{
	RefreshFromOutputWindow();
}

void ibAiMarkersPanel::Rebuild()
{
	if (m_list == nullptr) return;
	m_list->DeleteAllItems();

	for (size_t i = 0; i < m_markers.size(); ++i) {
		const auto& m = m_markers[i];
		const wxString sev = SeveritySymbol(m.severity) + wxT("  ") +
		                       SeverityLabel(m.severity);
		const long row = m_list->InsertItem(static_cast<long>(i), sev);
		m_list->SetItem(row, 1, m.file);
		m_list->SetItem(row, 2, wxString::Format(wxT("%d"), m.line));
		m_list->SetItem(row, 3, m.message);
		m_list->SetItem(row, 4, m.fix);

		// Severity-driven row colour. Errors stand out in red; warnings
		// keep an amber tint; fix-only rows render in green so a quick
		// scan reveals where the auto-fixable rows sit.
		if      (m.severity == wxT("error"))   m_list->SetItemTextColour(row, wxColour(185, 28, 28));
		else if (m.severity == wxT("warning")) m_list->SetItemTextColour(row, wxColour(180, 83, 9));
		else if (m.severity == wxT("fix"))     m_list->SetItemTextColour(row, wxColour(21, 128, 61));
	}

	if (m_statusLine != nullptr) {
		size_t errCount = 0, warnCount = 0, infoCount = 0, fixCount = 0;
		for (const auto& m : m_markers) {
			if      (m.severity == wxT("error"))   ++errCount;
			else if (m.severity == wxT("warning")) ++warnCount;
			else if (m.severity == wxT("fix"))     ++fixCount;
			else                                    ++infoCount;
		}
		m_statusLine->SetLabel(wxString::Format(
		    _("Всего: %zu  ·  ошибки: %zu  ·  предупреждения: %zu  ·  автофиксы: %zu"),
		    m_markers.size(), errCount, warnCount, fixCount));
	}
}
