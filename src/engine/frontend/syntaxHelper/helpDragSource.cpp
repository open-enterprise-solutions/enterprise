/////////////////////////////////////////////////////////////////////////////
// ibHelpDragSource — see header for the contract.
/////////////////////////////////////////////////////////////////////////////

#include "frontend/syntaxHelper/helpDragSource.h"

#include "backend/syntaxHelper/helpCorpus.h"
#include "backend/syntaxHelper/helpEntry.h"
#include "backend/compiler/compileCode.h"

#include <wx/dnd.h>
#include <wx/dataobj.h>
#include <wx/settings.h>

#include <cstdlib>

void ibHelpDragSource::Bind(wxListBox* list,
                              const std::vector<wxString>* ids,
                              const std::shared_ptr<const ibHelpCorpus>* corpus)
{
	m_list = list;
	m_ids = ids;
	m_corpus = corpus;
	if (m_list == nullptr) return;
	m_list->Bind(wxEVT_LEFT_DOWN, &ibHelpDragSource::OnLeftDown, this);
	m_list->Bind(wxEVT_MOTION,    &ibHelpDragSource::OnMotion,   this);
}

void ibHelpDragSource::OnLeftDown(wxMouseEvent& event)
{
	m_armed = false;
	m_dragId.clear();
	if (m_list != nullptr && m_ids != nullptr) {
		m_dragStart = event.GetPosition();
		const int idx = m_list->HitTest(m_dragStart);
		if (idx >= 0 && idx < static_cast<int>(m_ids->size())) {
			m_dragId = (*m_ids)[idx];
			m_armed  = true;
		}
	}
	event.Skip();
}

void ibHelpDragSource::OnMotion(wxMouseEvent& event)
{
	if (!m_armed || !event.LeftIsDown() || m_corpus == nullptr) {
		event.Skip();
		return;
	}

	// DPI-aware threshold. wxSYS_DRAG_X / wxSYS_DRAG_Y are platform-
	// computed slop in physical pixels; fall back to a 4-DIP square
	// when the system metric is unavailable.
	int slopX = wxSystemSettings::GetMetric(wxSYS_DRAG_X);
	int slopY = wxSystemSettings::GetMetric(wxSYS_DRAG_Y);
	if (slopX <= 0) slopX = m_list ? m_list->FromDIP(4) : 4;
	if (slopY <= 0) slopY = m_list ? m_list->FromDIP(4) : 4;

	const wxPoint delta = event.GetPosition() - m_dragStart;
	if (std::abs(delta.x) < slopX && std::abs(delta.y) < slopY) {
		event.Skip();
		return;
	}

	// Capture every value we need into locals BEFORE running the
	// modal drag pump. DoDragDrop on macOS spins its own event loop;
	// if the enclosing pane is closed during the drag, touching any
	// member of `this` after the call would dereference freed memory.
	if (m_corpus->get() == nullptr) {
		m_armed = false;
		return;
	}
	const ibHelpEntry* entry = (*m_corpus)->FindById(m_dragId);
	if (entry == nullptr) { m_armed = false; m_dragId.clear(); return; }

	const wxString tpl = entry->InsertTemplate(ibCompileCode::GetCodeStyle());
	if (tpl.IsEmpty()) { m_armed = false; m_dragId.clear(); return; }

	// Reset state BEFORE the modal call so a subsequent dtor of `this`
	// during the pump leaves nothing to write to.
	m_armed = false;
	m_dragId.clear();
	wxListBox* source = m_list;

	wxTextDataObject payload(tpl);
	wxDropSource src(payload, source);
	src.DoDragDrop(wxDrag_DefaultMove);
	// Do NOT touch any member of `this` past this line — `this` may
	// have been destroyed by the modal event pump.
}
