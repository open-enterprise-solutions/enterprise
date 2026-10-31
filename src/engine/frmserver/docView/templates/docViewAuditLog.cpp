////////////////////////////////////////////////////////////////////////////
//	Description : Registration journal — doc/view tab
////////////////////////////////////////////////////////////////////////////

#include "docViewAuditLog.h"

#include "backend/appData.h"
#include "backend/logger/logger.h"
#include "backend/metaCollection/partial/reference/reference.h"   // ibValueReferenceDataObject — a row's object
#include "core/serialize/dataBuilder.h"

#include "frmserver/client/clientFrame.h"        // ibClientFrame::Message

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
	return view->GetDocChildFrame() != nullptr ? view->GetDocChildFrame()->GetFrame() : nullptr;
}

// A time on the wire — ms since the epoch, past what an std::int32_t keeps.
static long long MsOf(const ibDataNode& node, const ibString& name)
{
	long long ms = 0;
	node.GetValue<ibNumber>(name).ToInt(ms);
	return ms;
}

bool ibAuditLogView::Fetch(const ibDataNode& request, ibDataNode& response)
{
	const ibAuditLogDocument* const document = dynamic_cast<const ibAuditLogDocument*>(GetDocument());
	ibLoggerReader* const reader = document != nullptr ? document->GetReader() : nullptr;
	if (reader == nullptr)
		return false;

	ibLogFilter filter;
	filter.from_ms = MsOf(request, ibProtocol::ibView::ibAuditLog::From);
	filter.to_ms = MsOf(request, ibProtocol::ibView::ibAuditLog::To);
	filter.min_level = -1;
	request.GetValue(ibProtocol::ibView::ibAuditLog::Level, filter.min_level);
	filter.user_name = request.GetValue<ibString>(ibProtocol::ibView::ibAuditLog::User);
	filter.session_id = request.GetValue<ibString>(ibProtocol::ibView::ibAuditLog::Session);
	filter.source = request.GetValue<ibString>(ibProtocol::ibView::ibAuditLog::Source);
	filter.event_type = request.GetValue<ibString>(ibProtocol::ibEvent::Event);
	filter.ref_guid = request.GetValue<ibString>(ibProtocol::ibView::ibAuditLog::Ref);
	filter.search = request.GetValue<ibString>(ibProtocol::ibView::ibAuditLog::Search);
	filter.offset = static_cast<std::size_t>(request.GetValue<std::int32_t>(ibProtocol::ibView::ibAuditLog::Offset));
	filter.limit = static_cast<std::size_t>(request.GetValue<std::int32_t>(ibProtocol::ibView::ibAuditLog::Count));

	for (const ibLogRow& entry : reader->Query(filter)) {
		ibDataNode& row = response.AddChild(0, 0);
		row.SetValue(ibProtocol::ibView::ibAuditLog::Time, ibNumber(static_cast<long long>(entry.ts_ms)));
		row.SetValue(ibProtocol::ibView::ibAuditLog::Level, static_cast<std::int32_t>(entry.level));
		row.SetValue(ibProtocol::ibView::ibAuditLog::Session, entry.session_id);
		row.SetValue(ibProtocol::ibView::ibAuditLog::User, entry.user_name);
		row.SetValue(ibProtocol::ibView::ibAuditLog::Host, entry.host);
		row.SetValue(ibProtocol::ibView::ibAuditLog::Source, entry.source);
		row.SetValue(ibProtocol::ibEvent::Event, entry.event_type);
		row.SetValue(ibProtocol::ibView::ibAuditLog::Message, entry.message);
		row.SetValue(ibProtocol::ibView::ibAuditLog::Ref, entry.ref_guid);
		row.SetValue(ibProtocol::ibView::ibAuditLog::RefMeta, static_cast<std::int32_t>(entry.ref_meta_id));
	}
	return true;
}

bool ibAuditLogView::OnClientEvent(ibProtocol::ibEvent::ibKind event, const ibDataNode& args)
{
	const ibAuditLogDocument* const document = dynamic_cast<const ibAuditLogDocument*>(GetDocument());
	const ibString ref = args.GetValue<ibString>(ibProtocol::ibView::ibAuditLog::Ref);
	if (document == nullptr || event != ibProtocol::ibEvent::ibKind::Open || ref.IsEmpty())
		return false;

	const ibGuid guid(ref);
	const ibMetaID metaId = args.GetValue<std::int32_t>(ibProtocol::ibView::ibAuditLog::RefMeta);
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
