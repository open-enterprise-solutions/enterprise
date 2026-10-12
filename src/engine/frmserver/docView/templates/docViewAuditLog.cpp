////////////////////////////////////////////////////////////////////////////
//	Description : Registration journal — doc/view tab
////////////////////////////////////////////////////////////////////////////

#include "docViewAuditLog.h"

#include "backend/appData.h"
#include "backend/logger/logger.h"
#include "backend/metaCollection/partial/reference/reference.h"   // ibValueReferenceDataObject — a row's object
#include "backend/pictureDescription.h"
#include "backend/picturePredefined.h"
#include "core/serialize/dataBuilder.h"

#include "frmserver/client/clientChildFrame.h"   // ibClientChildFrame — the tab, and the client it is drawn for
#include "frmserver/client/clientFrame.h"        // ibClientFrame::SendPicture, Message

// ============================================================
//   ibAuditLogDocument
// ============================================================

wxIMPLEMENT_DYNAMIC_CLASS(ibAuditLogDocument, ibDocument);

ibAuditLogDocument::ibAuditLogDocument(const ibMetaData* metaData) : ibDocument(), m_metaData(metaData)
{
	// ibDocument default ctor already leaves m_documentParent null, so
	// IsChildDocument() returns false — no flag to set.

	if (appData != nullptr && appData->GetLogger() != nullptr) {
		m_reader = std::make_unique<ibLoggerReader>(
			appData->GetLogger()->GetLogDir());
	}
}

// ============================================================
//   ibAuditLogView
// ============================================================

wxIMPLEMENT_DYNAMIC_CLASS(ibAuditLogView, ibView);

// The client this view's tab is drawn for.
static ibClientFrame* ClientOf(const ibView* view)
{
	const ibClientChildFrame* const tab = dynamic_cast<const ibClientChildFrame*>(view->GetDocChildFrame());
	return tab != nullptr ? tab->GetFrame() : nullptr;
}

// A time on the wire — ms since the epoch, past what an s32 keeps.
static wxLongLong_t MsOf(const ibDataNode& node, const wxString& name)
{
	long long ms = 0;
	node.GetValue<ibNumber>(name).ToInt(ms);
	return ms;
}

void ibAuditLogView::OnDraw(ibDataNode& WXUNUSED(frame))
{
	// THE TOOLBAR'S PICTURES, by their numbers — the client's view takes them as the desktop's did
	// (ibBackendPicture::GetPicture); the frame gives each once.
	if (ibClientFrame* const client = ClientOf(this)) {
		for (const ibPictureID id : { g_picFilterSetCLSID, g_picFilterClearCLSID, g_picUpdateFormCLSID, g_picSelectCLSID })
			client->SendPicture(ibPictureDescription(id), nullptr);
	}
}

bool ibAuditLogView::Fetch(const ibDataNode& request, ibDataNode& response)
{
	const ibAuditLogDocument* const document = dynamic_cast<const ibAuditLogDocument*>(GetDocument());
	ibLoggerReader* const reader = document != nullptr ? document->GetReader() : nullptr;
	if (reader == nullptr)
		return false;

	ibLogFilter filter;
	filter.from_ms = MsOf(request, wxT("From"));
	filter.to_ms = MsOf(request, wxT("To"));
	filter.min_level = -1;
	request.GetValue(wxT("Level"), filter.min_level);
	filter.user_name = request.GetValue<wxString>(wxT("User"));
	filter.session_id = request.GetValue<wxString>(wxT("Session"));
	filter.source = request.GetValue<wxString>(wxT("Source"));
	filter.event_type = request.GetValue<wxString>(wxT("Event"));
	filter.ref_guid = request.GetValue<wxString>(wxT("Ref"));
	filter.search = request.GetValue<wxString>(wxT("Search"));
	filter.offset = static_cast<std::size_t>(request.GetValue<s32>(wxT("Offset")));
	filter.limit = static_cast<std::size_t>(request.GetValue<s32>(wxT("Count")));

	for (const ibLogRow& entry : reader->Query(filter)) {
		ibDataNode& row = response.AddChild(0, 0);
		row.SetValue(wxT("Time"), ibNumber(static_cast<long long>(entry.ts_ms)));
		row.SetValue(wxT("Level"), static_cast<s32>(entry.level));
		row.SetValue(wxT("Session"), entry.session_id);
		row.SetValue(wxT("User"), entry.user_name);
		row.SetValue(wxT("Host"), entry.host);
		row.SetValue(wxT("Source"), entry.source);
		row.SetValue(wxT("Event"), entry.event_type);
		row.SetValue(wxT("Message"), entry.message);
		row.SetValue(wxT("Ref"), entry.ref_guid);
		row.SetValue(wxT("RefMeta"), static_cast<s32>(entry.ref_meta_id));
	}
	return true;
}

bool ibAuditLogView::OnClientEvent(ibProtocolEvent event, const ibDataNode& args)
{
	const ibAuditLogDocument* const document = dynamic_cast<const ibAuditLogDocument*>(GetDocument());
	const wxString ref = args.GetValue<wxString>(wxT("Ref"));
	if (document == nullptr || event != ibProtocolEvent::Open || ref.IsEmpty())
		return false;

	const ibGuid guid(ref);
	const ibMetaID metaId = args.GetValue<s32>(wxT("RefMeta"));
	if (metaId == 0) {
		// Convention from Phase 2g — system-table sys_user row. The desktop opened the User Admin dialog keyed by
		// guid (sys_user is not a metaobject); that dialog is not the server's yet — the person is told so.
		if (ibClientFrame* const client = ClientOf(this))
			client->Message(_("A user's card is not opened from the journal yet"), ibStatusMessage_Warning);
		return true;
	}

	// Metadata-backed object — Catalog / Document / ChartOf*.
	if (document->GetMetaData() == nullptr)
		return false;
	ibValueReferenceDataObject* refVal =
		ibValueReferenceDataObject::Create(document->GetMetaData(), metaId, guid);
	if (refVal != nullptr)
		refVal->ShowValue();
	return true;
}
