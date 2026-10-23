#include "queryConstructorRequest.h"

#include "backend/backend_exception.h"
#include "backend/backend_picture.h"                      // ibBackendPicture::SendPicture — a kind's picture by its id
#include "backend/compiler/typeCtor.h"                    // ibCtorAbstractType — a kind's class, for its picture
#include "backend/metaData.h"
#include "backend/query/queryLowering.h"                  // CheckNames / UngroupedProjections — the desktop's verdict
#include "backend/query/queryConstructorModel.h"          // the desktop's model — what is asked is answered by it
#include "backend/query/queryable.h"                      // ibSourceMetaDataScope — the names resolve against THIS configuration
#include "core/query/queryParser.h"                       // ibQueryParser — the source as the client wrote it
#include "core/query/queryRender.h"                       // ibRenderQueryExpr — a projection, as the window reads it back
#include "core/serialize/dataBuilder.h"
#include "frmserver/win/dlgs/settings/settingsRequest.h"  // ibWriteSettingsField / References — a field as the windows read it
#include "protocol/protocol.h"

#include <set>
#include <vector>

namespace qc = ibProtocol::ibView::ibQueryConstructor;

namespace {

// THE SOURCE AS A FROM WRITES IT, its arguments too (`AccumulationRegister.Stock.Turnovers(&From, &To, Record)`) — read
// by the parser, as the desktop window holds it. The package keeps the source alive.
const ibQuerySource* ReadSource(const ibString& text, ibQueryPackage& package)
{
	package = ibQueryParser().ParsePackage(wxT("SELECT * FROM ") + text);
	if (package.m_statements.empty() || !package.m_statements.front().m_select)
		return nullptr;
	return &package.m_statements.front().m_select->m_from;
}

void WriteSources(ibDataNode& response, const ibQueryConstructorModel& model, const ibMetaData* metaData)
{
	ibDataNode& sources = response.Child(qc::Sources);
	// ⭐ EACH KIND'S PICTURE, ONCE — on the kind's row, never on its tables': a table wears its kind's (the desktop's
	// KindIcon, GetAvailableCtor(kind)->GetClassIcon), the way the LINQ constructor's Kinds carry it.
	ibDataNode& kinds = response.Child(qc::Kinds);
	std::set<wxString> kindsWritten;
	for (const ibQueryConstructorSource& source : model.GetSources()) {
		if (!source.m_temp && !source.m_path.empty() && kindsWritten.insert(source.m_path.front()).second) {
			ibDataNode& kind = kinds.AddChild(0, 0);
			kind.SetValue(qc::Name, source.m_path.front());
			const wxString picture = ibKindPicture(source.m_path.front(), metaData);
			if (!picture.IsEmpty())
				kind.SetValue(qc::Picture, picture);
		}
		ibDataNode& row = sources.AddChild(0, 0);
		std::vector<ibDataValue> path;
		for (const wxString& segment : source.m_path)
			path.push_back(ibDataValue::String(segment));
		row.AddField(qc::Path, ibDataValue::Array(std::move(path)));
		row.SetValue(qc::Presentation, source.m_presentation);
		// ⭐ ITS OWN REFERENCE TYPE — what a CAST to the table brings a value to and what a reference's branch is named
		// by; the client's model answers both from this list (ReferenceOfTable, TableOfReference).
		if (const ibClassID reference = model.ReferenceOfTable(source.m_path))
			row.SetValue(qc::Reference, ibString::Format(wxT("%llu"), static_cast<unsigned long long>(reference)));
	}
}

void WriteFields(ibDataNode& response, const std::vector<ibQueryConstructorField>& fields, const ibMetaData* metaData)
{
	ibDataNode& written = response.Child(qc::Fields);
	std::vector<ibTypeDescription> types;
	for (const ibQueryConstructorField& field : fields) {
		ibWriteSettingsField(written, field.m_name, field.m_caption, static_cast<ibMetaID>(wxNOT_FOUND), field.m_type,
			field.m_available, field.m_picture, metaData);
		types.push_back(field.m_type);
	}
	ibWriteSettingsReferences(response.Child(qc::References), types, metaData);
}

void WriteParameters(ibDataNode& response, const std::vector<ibQuerySourceParameter>& parameters)
{
	ibDataNode& written = response.Child(qc::Parameters);
	for (const ibQuerySourceParameter& parameter : parameters) {
		ibDataNode& row = written.AddChild(0, 0);
		row.SetValue(qc::Name, parameter.m_name);
		row.SetValue(qc::Condition, parameter.m_condition);
		ibDataValue type;
		ibTypeDescriptionMemory::WriteNode(type, parameter.m_type);
		row.SetProperty(qc::Type, type);
		row.SetValue(qc::Required, parameter.m_required);
		std::vector<ibDataValue> choices;
		for (const wxString& choice : parameter.m_choices)
			choices.push_back(ibDataValue::String(choice));
		row.AddField(qc::Choices, ibDataValue::Array(std::move(choices)));
		row.SetValue(qc::Description, parameter.m_description);
		row.SetValue(qc::ConsumedBySource, parameter.m_consumedBySource);
		row.SetValue(qc::Default, parameter.m_default);
	}
}

// THE SELECT THE WINDOW IS ON — its Current(): a statement's select, or one of its union branches.
const ibQuerySelect* SelectAt(const ibQueryPackage& package, std::int32_t statement, std::int32_t unionBranch)
{
	if (statement < 0 || static_cast<size_t>(statement) >= package.m_statements.size())
		return nullptr;
	const ibQuerySelect* const select = package.m_statements[static_cast<size_t>(statement)].m_select.get();
	if (select == nullptr || unionBranch < 0)
		return select;
	return static_cast<size_t>(unionBranch) < select->m_unions.size()
		? select->m_unions[static_cast<size_t>(unionBranch)].get() : select;
}

// CHECK — the desktop window's two calls into the engine, each answered as it is asked there: the whole text's
// CheckNames (its verdict line, OK, the Check button), or, with the select named the way Fields names a Slot, that
// select's UngroupedProjections (OnAddGrouping).
void WriteCheck(ibDataNode& response, const ibDataNode& request)
{
	ibQueryPackage package;
	try {
		package = ibQueryParser().ParsePackage(request.GetValue<ibString>(qc::Text));
		if (request.FindField(qc::Statement) == nullptr) {
			ibQueryLowering::CheckNames(package, std::map<wxString, ibValue>());
			return;
		}
	}
	catch (const ibCoreException& error) {
		response.SetValue(qc::Error, ibString(error.GetErrorDescription()));
		return;
	}

	std::vector<ibDataValue> projections;
	const ibQuerySelect* const select =
		SelectAt(package, request.GetValue<std::int32_t>(qc::Statement), request.GetValue<std::int32_t>(qc::UnionBranch));
	if (select != nullptr)
		for (const ibQueryAstExprPtr& missing : ibQueryLowering::UngroupedProjections(*select, std::map<wxString, ibValue>()))
			if (missing)
				projections.push_back(ibDataValue::String(ibRenderQueryExpr(*missing)));
	response.AddField(qc::UngroupedProjections, ibDataValue::Array(std::move(projections)));
}

} // namespace

wxString ibKindPicture(const wxString& kind, const ibMetaData* metaData)
{
	if (metaData == nullptr || kind.IsEmpty())
		return wxString();
	const ibCtorAbstractType* const ctor = metaData->GetAvailableCtor(kind);
	return ctor != nullptr ? wxString(ibBackendPicture::SendPicture(ctor->GetClassType())) : wxString();
}

bool ibFetchQueryConstructorRequest(const ibDataNode& request, ibDataNode& response, const ibMetaData* metaData)
{
	const qc::ibRequest kind = static_cast<qc::ibRequest>(request.GetValue<std::int32_t>(qc::Kind));
	if (kind != qc::ibRequest::Sources && kind != qc::ibRequest::Fields && kind != qc::ibRequest::SourceParameters
	    && kind != qc::ibRequest::Check)
		return false;

	const ibSourceMetaDataScope scope(metaData);
	if (kind == qc::ibRequest::Check) {
		WriteCheck(response, request);
		return true;
	}
	const ibQueryConstructorModel model(metaData);
	if (kind == qc::ibRequest::Sources) {
		WriteSources(response, model, metaData);
		return true;
	}

	try {
		ibQueryPackage package;
		const ibQuerySource* const source = ReadSource(request.GetValue<ibString>(qc::Source), package);
		if (source == nullptr)
			return true;
		if (kind == qc::ibRequest::SourceParameters)
			WriteParameters(response, model.GetSourceParameters(*source));
		else if (request.FindField(qc::Slot) != nullptr)
			WriteFields(response, model.GetConditionFields(*source, request.GetValue<ibString>(qc::Slot)), metaData);
		else
			WriteFields(response, model.GetFields(*source, package, 0), metaData);
	}
	catch (const ibCoreException& error) {
		// A source the parser refuses offers nothing — said, so the client knows why.
		response.SetValue(qc::Error, ibString(error.GetErrorDescription()));
	}
	return true;
}
