#include "docViewProfile.h"

#include <cstdint>

#include "backend/metaCollection/metaObject.h"   // g_metaModuleCLSID — its picture: lines of modules
#include "core/serialize/dataBuilder.h"

#include "frmserver/client/clientFrame.h"   // the frame the tab opens in, told it changed (RefreshFrame)
#include "frmserver/docView/docManager.h"
#include "protocol/protocolDesigner.h"      // ibProtocol::ibView::ibProfile

// Its title says how long it ran and how much of it was script — every line's own time, of a call or of none, added.
ibProfileDocument::ibProfileDocument(const ibProfileData& profile) : ibDocument(), m_profile(profile)
{
	std::int64_t spent = 0;
	for (const ibProfileData::ibProfileCall& call : m_profile.m_calls) {
		for (const ibProfileData::ibProfileRow& row : call.m_rows)
			spent += row.m_self;
	}
	for (const ibProfileData::ibProfileRow& row : m_profile.m_rows)
		spent += row.m_self;

	SetTitle(ibString::Format(_("Performance measurement: %s s, script %s s"),
		ibString::Format(ibT("%.3f"), m_profile.m_duration / 1000000.0),
		ibString::Format(ibT("%.3f"), spent / 1000000.0)));
	SetIcon(ibPictureDescription(g_metaModuleCLSID));
}

// What opens it is no call of the client — the engine's news, the assistant's tool — so the client is told its frame
// changed.
ibProfileDocument* ibProfileDocument::ShowProfile(const ibProfileData& profile)
{
	ibClientFrame* const frame = ibClientFrame::GetFrame();
	ibDocManager* const documentManager = frame != nullptr ? frame->GetDocumentManager() : nullptr;
	if (documentManager == nullptr)
		return nullptr;

	ibProfileDocument* const doc = documentManager->CreateDocument<ibProfileDocument>(profile);
	if (doc == nullptr)
		return nullptr;
	documentManager->AddDocument(doc);
	if (!doc->OnCreate(ibEmptyString, 0)) {
		doc->DeleteAllViews();
		return nullptr;
	}
	frame->RefreshFrame();
	return doc;
}

bool ibProfileView::Fetch(const ibDataNode& /*request*/, ibDataNode& response)
{
	const ibProfileDocument* const document = dynamic_cast<const ibProfileDocument*>(GetDocument());
	if (document == nullptr)
		return false;

	// A statement as the database got it, under a query or straight under the line.
	const auto writeStatements = [](ibDataNode& into, const std::vector<ibQueryDebugData::ibStatement>& statements) {
		ibDataNode& written = into.Child(ibProtocol::ibView::ibProfile::Statements);
		for (const ibQueryDebugData::ibStatement& statement : statements) {
			ibDataNode& row = written.AddChild(0, 0);
			row.SetValue(ibProtocol::ibView::ibProfile::Text, statement.m_text);
			row.SetValue(ibProtocol::ibView::ibProfile::Plan, statement.m_plan);
			row.SetValue(ibProtocol::ibView::ibProfile::Dbms, statement.m_dbms);
			row.AddField(ibProtocol::ibView::ibProfile::Count, ibDataValue::UInt(statement.m_count));
			row.AddField(ibProtocol::ibView::ibProfile::RowCount, ibDataValue::UInt(statement.m_rows));
			row.AddField(ibProtocol::ibView::ibProfile::Run, ibDataValue::Int(statement.m_run));
			row.AddField(ibProtocol::ibView::ibProfile::Fetch, ibDataValue::Int(statement.m_fetch));
			row.AddField(ibProtocol::ibView::ibProfile::Slowest, ibDataValue::Int(statement.m_slowest));
			ibDataNode& values = row.Child(ibProtocol::ibView::ibProfile::Values);
			for (const ibString& value : statement.m_values)
				values.AddChild(0, 0).SetValue(ibProtocol::ibView::ibProfile::Text, value);
		}
	};

	// The lines — of a call, or of none.
	const auto writeRows = [&writeStatements](ibDataNode& into, const std::vector<ibProfileData::ibProfileRow>& lines) {
		ibDataNode& rows = into.Child(ibProtocol::ibView::ibProfile::Rows);
		for (const ibProfileData::ibProfileRow& line : lines) {
			ibDataNode& row = rows.AddChild(0, 0);
			row.SetValue(ibProtocol::ibView::ibProfile::Module, line.m_moduleName);
			row.SetValue(ibProtocol::ibView::ibProfile::Name, line.m_name);
			row.SetValue(ibProtocol::ibView::ibProfile::Procedure, line.m_procedure);
			row.SetValue(ibProtocol::ibView::ibProfile::Line, static_cast<std::int32_t>(line.m_line));
			row.AddField(ibProtocol::ibView::ibProfile::Count, ibDataValue::UInt(line.m_count));
			row.AddField(ibProtocol::ibView::ibProfile::Self, ibDataValue::Int(line.m_self));
			row.AddField(ibProtocol::ibView::ibProfile::Total, ibDataValue::Int(line.m_total));

			ibDataNode& queries = row.Child(ibProtocol::ibView::ibProfile::Queries);
			for (const ibQueryDebugData::ibQuery& query : line.m_queries.m_queries) {
				ibDataNode& written = queries.AddChild(0, 0);
				written.SetValue(ibProtocol::ibView::ibProfile::Source, query.m_source);
				written.AddField(ibProtocol::ibView::ibProfile::Count, ibDataValue::UInt(query.m_count));
				written.AddField(ibProtocol::ibView::ibProfile::Total, ibDataValue::Int(query.m_total));
				written.AddField(ibProtocol::ibView::ibProfile::Database, ibDataValue::Int(query.m_database));
				writeStatements(written, query.m_statements);
			}
			writeStatements(row, line.m_queries.m_statements);
		}
	};

	const ibProfileData& profile = document->GetProfile();
	response.AddField(ibProtocol::ibView::ibProfile::Duration, ibDataValue::Int(profile.m_duration));
	ibDataNode& calls = response.Child(ibProtocol::ibView::ibProfile::Calls);
	for (const ibProfileData::ibProfileCall& call : profile.m_calls) {
		ibDataNode& written = calls.AddChild(0, 0);
		written.SetValue(ibProtocol::ibView::ibProfile::Method, call.m_method);
		written.AddField(ibProtocol::ibView::ibProfile::Count, ibDataValue::UInt(call.m_count));
		written.AddField(ibProtocol::ibView::ibProfile::Total, ibDataValue::Int(call.m_total));
		written.AddField(ibProtocol::ibView::ibProfile::RequestBytes, ibDataValue::UInt(call.m_requestBytes));
		written.AddField(ibProtocol::ibView::ibProfile::AnswerBytes, ibDataValue::UInt(call.m_answerBytes));
		writeRows(written, call.m_rows);
	}
	writeRows(response, profile.m_rows);
	return true;
}
