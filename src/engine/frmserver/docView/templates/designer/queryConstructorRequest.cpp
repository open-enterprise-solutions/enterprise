#include "queryConstructorRequest.h"

#include "backend/backend_exception.h"
#include "backend/metaData.h"
#include "backend/query/queryConstructorModel.h"          // the desktop's model — what is asked is answered by it
#include "backend/query/queryable.h"                      // ibSourceMetaDataScope — the names resolve against THIS configuration
#include "core/clsid.h"                                   // IsReference — a table's own reference type
#include "core/query/queryParser.h"                       // ibQueryParser — the source as the client wrote it
#include "core/serialize/dataBuilder.h"
#include "frmserver/win/dlgs/settings/settingsRequest.h"  // ibWriteSettingsField / References — a field as the windows read it
#include "protocol/protocol.h"

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

void WriteSources(ibDataNode& response, const ibQueryConstructorModel& model)
{
	ibDataNode& sources = response.Child(qc::Sources);
	for (const ibQueryConstructorSource& source : model.GetSources()) {
		ibDataNode& row = sources.AddChild(0, 0);
		std::vector<ibDataValue> path;
		for (const wxString& segment : source.m_path)
			path.push_back(ibDataValue::String(segment));
		row.AddField(qc::Path, ibDataValue::Array(std::move(path)));
		row.SetValue(qc::Presentation, source.m_presentation);
		// ⭐ ITS OWN REFERENCE TYPE — what a CAST to the table brings a value to (TypeOfCast) and what a reference's
		// branch is named by (GetReferenceBranches); the client answers both from this list.
		if (source.m_path.size() == 2) {
			auto cast = ibQueryAstExpr::Make(ibQueryAstExprKind::Cast);
			cast->m_path = source.m_path;
			const ibTypeDescription type = model.TypeOfCast(*cast);
			if (type.GetClsidCount() == 1 && IsReference(type.GetClsidList().front()))
				row.SetValue(qc::Reference,
					ibString::Format(wxT("%llu"), static_cast<unsigned long long>(type.GetClsidList().front())));
		}
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

} // namespace

bool ibFetchQueryConstructorRequest(const ibDataNode& request, ibDataNode& response, const ibMetaData* metaData)
{
	const qc::ibRequest kind = static_cast<qc::ibRequest>(request.GetValue<s32>(qc::Kind));
	if (kind != qc::ibRequest::Sources && kind != qc::ibRequest::Fields && kind != qc::ibRequest::SourceParameters)
		return false;

	const ibSourceMetaDataScope scope(metaData);
	const ibQueryConstructorModel model(metaData);
	if (kind == qc::ibRequest::Sources) {
		WriteSources(response, model);
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
