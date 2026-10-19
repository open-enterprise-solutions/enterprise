#include "loggerReader.h"

#include "frmclient/mainFrame/mainFrame.h"
#include "protocol/protocol.h"

std::vector<ibLogRow> ibLoggerReader::Query(const ibLogFilter& filter)
{
	std::vector<ibLogRow> rows;
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (frame == nullptr)
		return rows;

	ibProtocolNode request;
	request.SetValue(ibProtocol::ibView::ibAuditLog::From, static_cast<long long>(filter.from_ms))
		.SetValue(ibProtocol::ibView::ibAuditLog::To, static_cast<long long>(filter.to_ms))
		.SetValue(ibProtocol::ibView::ibAuditLog::Level, filter.min_level)
		.SetValue(ibProtocol::ibView::ibAuditLog::User, filter.user_name)
		.SetValue(ibProtocol::ibView::ibAuditLog::Session, filter.session_id)
		.SetValue(ibProtocol::ibView::ibAuditLog::Source, filter.source)
		.SetValue(ibProtocol::ibView::ibAuditLog::Event, filter.event_type)
		.SetValue(ibProtocol::ibView::ibAuditLog::Ref, filter.ref_guid)
		.SetValue(ibProtocol::ibView::ibAuditLog::Search, filter.search)
		.SetValue(ibProtocol::ibView::ibAuditLog::Offset, static_cast<long long>(filter.offset))
		.SetValue(ibProtocol::ibView::ibAuditLog::Count, static_cast<long long>(filter.limit));

	ibProtocolNode params, answer;
	params.SetValue(ibProtocol::ibFetch::Tab, m_tabId)
		.SetValue(ibProtocol::ibFetch::Request, request);
	if (!frame->Fetch(ibProtocol::ibMethod::Fetch, params, answer))
		return rows;

	for (const ibProtocolNode& entry : answer.Children()) {
		ibLogRow row;
		row.ts_ms = entry.GetInt(ibProtocol::ibView::ibAuditLog::Time);
		row.level = static_cast<int>(entry.GetInt(ibProtocol::ibView::ibAuditLog::Level));
		row.session_id = entry.GetString(ibProtocol::ibView::ibAuditLog::Session);
		row.user_name = entry.GetString(ibProtocol::ibView::ibAuditLog::User);
		row.host = entry.GetString(ibProtocol::ibView::ibAuditLog::Host);
		row.source = entry.GetString(ibProtocol::ibView::ibAuditLog::Source);
		row.event_type = entry.GetString(ibProtocol::ibView::ibAuditLog::Event);
		row.message = entry.GetString(ibProtocol::ibView::ibAuditLog::Message);
		row.ref_guid = entry.GetString(ibProtocol::ibView::ibAuditLog::Ref);
		row.ref_meta_id = static_cast<int>(entry.GetInt(ibProtocol::ibView::ibAuditLog::RefMeta));
		rows.push_back(std::move(row));
	}
	return rows;
}
