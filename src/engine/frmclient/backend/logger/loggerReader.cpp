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
	request.SetValue(ibProtocolName::From, static_cast<long long>(filter.from_ms))
		.SetValue(ibProtocolName::To, static_cast<long long>(filter.to_ms))
		.SetValue(ibProtocolName::Level, filter.min_level)
		.SetValue(ibProtocolName::User, filter.user_name)
		.SetValue(ibProtocolName::Session, filter.session_id)
		.SetValue(ibProtocolName::Source, filter.source)
		.SetValue(ibProtocolName::Event, filter.event_type)
		.SetValue(ibProtocolName::Ref, filter.ref_guid)
		.SetValue(ibProtocolName::Search, filter.search)
		.SetValue(ibProtocolName::Offset, static_cast<long long>(filter.offset))
		.SetValue(ibProtocolName::Count, static_cast<long long>(filter.limit));

	ibProtocolNode params, answer;
	params.SetValue(ibProtocolName::Tab, m_tabId)
		.SetValue(ibProtocolName::Request, request);
	if (!frame->Fetch(params, answer))
		return rows;

	for (const ibProtocolNode& entry : answer.Children()) {
		ibLogRow row;
		row.ts_ms = entry.GetInt(ibProtocolName::Time);
		row.level = static_cast<int>(entry.GetInt(ibProtocolName::Level));
		row.session_id = entry.GetString(ibProtocolName::Session);
		row.user_name = entry.GetString(ibProtocolName::User);
		row.host = entry.GetString(ibProtocolName::Host);
		row.source = entry.GetString(ibProtocolName::Source);
		row.event_type = entry.GetString(ibProtocolName::Event);
		row.message = entry.GetString(ibProtocolName::Message);
		row.ref_guid = entry.GetString(ibProtocolName::Ref);
		row.ref_meta_id = static_cast<int>(entry.GetInt(ibProtocolName::RefMeta));
		rows.push_back(std::move(row));
	}
	return rows;
}
