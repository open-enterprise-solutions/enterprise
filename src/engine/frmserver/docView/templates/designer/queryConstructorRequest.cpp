#include "queryConstructorRequest.h"

#include "backend/backend_exception.h"
#include "backend/backend_picture.h"                      // ibBackendPicture::SendPicture — a kind's picture by its id
#include "backend/compiler/typeCtor.h"                    // ibCtorAbstractType — a kind's class, for its picture
#include "backend/metaData.h"
#include "backend/query/queryLowering.h"                  // CheckNames / UngroupedProjections — the desktop's verdict
#include "backend/query/queryConstructorModel.h"          // the desktop's model — what is asked is answered by it
#include "backend/query/queryable.h"                      // ibSourceMetaDataScope — the names resolve against THIS configuration
#include "core/clsid.h"                                   // IsReference — a table's own reference type
#include "core/query/queryParser.h"                       // ibQueryParser — the source as the client wrote it
#include "core/query/queryRender.h"                       // ibRenderQueryPackage — the completed text, as the window writes it
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

// THE ENGINE'S VERDICT ON A TEXT — CheckNames' words; empty: it reads.
wxString VerdictOf(const ibQueryPackage& package)
{
	try {
		ibQueryLowering::CheckNames(package, std::map<wxString, ibValue>());
	}
	catch (const ibCoreException& error) {
		return error.GetErrorDescription();
	}
	return wxString();
}

// THE SELECT A GROUPING COMPLETES — the window's Current(): a statement's select, or one of its union branches.
ibQuerySelect* SelectAt(ibQueryPackage& package, s32 statement, s32 branch)
{
	if (statement < 0 || static_cast<size_t>(statement) >= package.m_statements.size())
		return nullptr;
	ibQuerySelect* const select = package.m_statements[static_cast<size_t>(statement)].m_select.get();
	if (select == nullptr || branch < 0)
		return select;
	return static_cast<size_t>(branch) < select->m_unions.size()
		? select->m_unions[static_cast<size_t>(branch)].get() : select;
}

// ⭐⭐ CHECK — the whole text, asked once per human action (an edit, OK, the Check button), never once per field. With
// a select named it also answers what that select's GROUP BY lacks, and the verdict on the text once they are added
// — the desktop's OnAddGrouping, so the verdict line after a grouping asks nothing more.
void WriteCheck(ibDataNode& response, const ibDataNode& request)
{
	ibQueryPackage package;
	try {
		package = ibQueryParser().ParsePackage(request.GetValue<ibString>(qc::Text));
	}
	catch (const ibCoreException& error) {
		response.SetValue(qc::Error, ibString(error.GetErrorDescription()));
		return;
	}

	const wxString verdict = VerdictOf(package);
	if (!verdict.IsEmpty())
		response.SetValue(qc::Error, ibString(verdict));

	if (request.FindField(qc::Statement) == nullptr)
		return;
	const s32 branch = request.FindField(qc::Branch) != nullptr ? request.GetValue<s32>(qc::Branch) : -1;
	ibQuerySelect* const select = SelectAt(package, request.GetValue<s32>(qc::Statement), branch);
	if (select == nullptr)
		return;

	std::vector<ibDataValue> ungrouped;
	std::vector<ibQueryAstExprPtr> keys;
	for (const ibQueryAstExprPtr& missing : ibQueryLowering::UngroupedProjections(*select, std::map<wxString, ibValue>())) {
		if (!missing)
			continue;
		const wxString text = ibRenderQueryExpr(*missing);
		ungrouped.push_back(ibDataValue::String(text));
		keys.push_back(ibQueryColumnFromPath(text));   // as the window adds them (OnAddGrouping)
	}
	response.AddField(qc::Ungrouped, ibDataValue::Array(std::move(ungrouped)));
	if (keys.empty())
		return;

	for (ibQueryAstExprPtr& key : keys)
		select->m_groupBy.push_back(std::move(key));
	ibDataNode& completed = response.Child(qc::Completed);
	completed.SetValue(qc::Text, ibString(ibRenderQueryPackage(package)));
	const wxString completedVerdict = VerdictOf(package);
	if (!completedVerdict.IsEmpty())
		completed.SetValue(qc::Error, ibString(completedVerdict));
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
	const qc::ibRequest kind = static_cast<qc::ibRequest>(request.GetValue<s32>(qc::Kind));
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
