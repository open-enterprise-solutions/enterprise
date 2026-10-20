#include "queryConstructorModel.h"

#include "frmclient/backend/metaCollection/partial/reference/reference.h"   // ibValueReferenceDataObject — the fields' [+]
#include "frmclient/docView/tabDocument.h"                                  // a tab's fetch — the fields of its text
#include "frmclient/mainFrame/mainFrame.h"
#include "frmclient/win/dlgs/settings/settingsFieldTree.h"   // ibReadSettingsFields — a field as the server writes it
#include "protocol/protocol.h"
#include "core/query/queryParser.h"   // ibQueryParser — the package is read here, as the desktop reads it
#include "core/query/queryRender.h"   // ibRenderQueryPackage — …and written back

#include <wx/msgdlg.h>
#include <algorithm>

#include <map>

namespace {

// WHAT A TAB'S VIEW ALREADY SAID OF A TEXT — its query's fields, kept by the tab's id: the panel reads them as it is
// built and again as each tree is filled, and the same text asks the server once.
struct ibKnownQueryFields {
	wxString       m_text;
	ibProtocolNode m_answer;
};

// …and a dynamic list's, by its main table and text.
struct ibKnownListFields {
	ibMetaID       m_mainTable = wxNOT_FOUND;
	wxString       m_text;
	ibProtocolNode m_answer;
};

std::map<long long, ibKnownListFields>& KnownListFields()
{
	static std::map<long long, ibKnownListFields> s_known;
	return s_known;
}

std::map<long long, ibKnownQueryFields>& KnownQueryFields()
{
	static std::map<long long, ibKnownQueryFields> s_known;
	return s_known;
}

// The question's fields, as a settings window's field tree reads them: the name, how the source calls it, the type,
// whether the options leave it. The constructor's tree shows the name.
std::vector<ibQueryConstructorField> ReadQueryFields(const ibProtocolNode& described)
{
	std::vector<ibQueryConstructorField> fields;
	for (ibSettingsPlainField& plain : ibReadSettingsFields(described)) {
		ibQueryConstructorField field;
		field.m_name = plain.m_name;
		field.m_presentation = plain.m_name;
		field.m_caption = plain.m_presentation;
		field.m_type = std::move(plain.m_type);
		field.m_available = plain.m_available;
		fields.push_back(std::move(field));
	}
	return fields;
}

} // namespace

void ibQueryFieldsKnown(long long tabId, const wxString& text, const ibProtocolNode& answer)
{
	KnownQueryFields()[tabId] = ibKnownQueryFields{ text, answer };
}

void ibQueryFieldsForget(long long tabId)
{
	KnownQueryFields().erase(tabId);
	KnownListFields().erase(tabId);
}

std::vector<ibSettingsPlainField> ibListSourceFields(ibMetaID mainTable, const wxString& text, const ibMetaData* metaData,
	wxString* error)
{
	if (error != nullptr)
		error->clear();

	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (frame == nullptr || metaData == nullptr || metaData->GetTabId() == 0)
		return {};
	if (mainTable == wxNOT_FOUND && wxString(text).Trim(true).Trim(false).IsEmpty())
		return {};

	const long long tabId = metaData->GetTabId();
	ibProtocolNode answer;
	const auto known = KnownListFields().find(tabId);
	if (known != KnownListFields().end() && known->second.m_mainTable == mainTable && known->second.m_text == text) {
		answer = known->second.m_answer;
	}
	else {
		const ibTabDocument* const document = frame->FindDocument(tabId);
		if (document == nullptr)
			return {};
		ibProtocolNode request;
		request.SetValue(ibProtocol::ibView::ibComposer::Kind, static_cast<long long>(ibProtocol::ibView::ibComposer::ibRequest::Source))
			.SetValue(ibProtocol::ibView::ibComposer::Text, text);
		if (mainTable != wxNOT_FOUND)
			request.SetValue(ibProtocol::ibView::ibComposer::Id, static_cast<long long>(mainTable));
		if (!document->Fetch(request, answer))
			return {};
		KnownListFields()[tabId] = ibKnownListFields{ mainTable, text, answer };
	}
	ibValueReferenceDataObject::ReadReferences(answer.FindChild(ibProtocol::ibView::ibComposer::References));
	if (error != nullptr)
		*error = answer.GetString(ibProtocol::ibView::ibComposer::Error);
	return ibReadSettingsFields(answer.FindChild(ibProtocol::ibView::ibComposer::Fields));
}

std::vector<ibQueryConstructorField> ibQueryFieldsOfText(const wxString& text, const ibMetaData* metaData, wxString* error)
{
	if (error != nullptr)
		error->clear();

	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (frame == nullptr)
		return {};

	// ⭐ A TAB'S CONFIGURATION ASKS ITS TAB — a composer's panel: the text is the one being edited there, so its fields
	// are the tab's answer for it (fetch {Tab, Request: {Kind: Fields, Text}}); the text the tab's view was drawn with
	// was said with the view, and asks nothing.
	if (metaData != nullptr && metaData->GetTabId() != 0) {
		// An empty text has no fields — a new composition's window opens without asking.
		if (wxString(text).Trim(true).Trim(false).IsEmpty())
			return {};
		const long long tabId = metaData->GetTabId();
		ibProtocolNode answer;
		const auto known = KnownQueryFields().find(tabId);
		if (known != KnownQueryFields().end() && known->second.m_text == text) {
			answer = known->second.m_answer;
		}
		else {
			const ibTabDocument* const document = frame->FindDocument(tabId);
			if (document == nullptr)
				return {};
			ibProtocolNode request;
			request.SetValue(ibProtocol::ibView::ibComposer::Kind, static_cast<long long>(ibProtocol::ibView::ibComposer::ibRequest::Fields))
				.SetValue(ibProtocol::ibView::ibComposer::Text, text);
			if (!document->Fetch(request, answer))
				return {};
			ibQueryFieldsKnown(tabId, text, answer);
		}
		ibValueReferenceDataObject::ReadReferences(answer.FindChild(ibProtocol::ibView::ibComposer::References));
		if (error != nullptr)
			*error = answer.GetString(ibProtocol::ibView::ibComposer::Error);
		return ReadQueryFields(answer.FindChild(ibProtocol::ibView::ibComposer::Fields));
	}

	// …A WINDOW THE SERVER ASKED reads the question's: the fields it described with it.
	return ReadQueryFields(frame->GetRequest().FindChild(ibProtocol::ibView::ibComposer::Fields));
}

//////////////////////////////////////////////////////////////////////
// ibQueryConstructorModel — the desktop's model, under its name
//////////////////////////////////////////////////////////////////////

namespace {

// ⚠ NOT HERE YET — said ONCE: the window asks the model at every edit, and a box at every keystroke would be noise.
void SayConfigurationNotHereYet()
{
	static bool s_said = false;
	if (s_said)
		return;
	s_said = true;
	wxMessageBox(_("The configuration's tables and fields do not reach the query constructor in this client yet. "
		"The query text, its temporary tables and its own expressions can be edited; the tables to add cannot be listed."),
		_("Query constructor"), wxOK | wxICON_INFORMATION);
}

} // namespace

ibQueryConstructorModel::ibQueryConstructorModel(const ibMetaData* metaData)
	: m_metaData(metaData)
{
}

// ⚠ NOT HERE YET — the configuration's sources are the server's (query-constructor-doors.md: request Sources).
std::vector<ibQueryConstructorSource> ibQueryConstructorModel::GetSources() const
{
	SayConfigurationNotHereYet();
	return {};
}

// ⚠ NOT HERE YET — a virtual table's arguments are its source's (query-constructor-doors.md: request SourceParameters).
std::vector<ibQuerySourceParameter> ibQueryConstructorModel::GetSourceParameters(const ibQuerySource& /*source*/) const
{
	return {};
}

// ⚠ NOT HERE YET — the client derives them from request Fields (query-constructor-doors.md).
std::vector<ibQueryConstructorField> ibQueryConstructorModel::GetConditionFields(const ibQuerySource& /*source*/,
	const wxString& /*slot*/) const
{
	return {};
}

std::vector<ibQueryConstructorSource> ibQueryConstructorModel::GetTempSources(
	const ibQueryPackage& package, size_t beforeStatement)
{
	std::vector<ibQueryConstructorSource> out;

	// ORDER IS THE WHOLE POINT: only what the statements BEFORE this one left is selectable.
	// A table made later does not exist yet, and offering it would produce a query that reads a
	// name nothing has filled.
	const size_t limit = std::min(beforeStatement, package.m_statements.size());
	for (size_t i = 0; i < limit; ++i) {
		const ibQueryAstStatement& statement = package.m_statements[i];

		if (statement.IsDrop()) {
			// A drop takes the name back out of the list — that is what "release early" MEANS,
			// and a constructor still offering it would be offering something already gone.
			const auto it = std::find_if(out.begin(), out.end(),
				[&statement](const ibQueryConstructorSource& s) {
					return !s.m_path.empty() && s.m_path[0].IsSameAs(statement.m_dropTemp, false);
				});
			if (it != out.end())
				out.erase(it);
			continue;
		}

		if (!statement.m_select)
			continue;

		// ⭐ A NAMED RESULT IS SELECTABLE TOO — a query result link. `ONTO Sales` makes `Sales` a name a
		// later statement can read, and reading it is what a LINK is: the package resolves the name
		// into that statement's own select, so the join runs in the DBMS.
		//
		// Listed beside the temp tables because from the constructor's side they are the same
		// question — "what may this statement select from that the package itself made" — and the
		// difference (a table versus a named result) is the package's business, not the tree's.
		const wxString& name = statement.m_select->m_intoTemp.IsEmpty()
			? statement.m_select->m_ontoName : statement.m_select->m_intoTemp;
		if (name.IsEmpty())
			continue;

		ibQueryConstructorSource source;
		source.m_path.push_back(name);
		source.m_presentation = name;
		source.m_temp = true;
		source.m_namedResult = statement.m_select->m_intoTemp.IsEmpty();   // named by ONTO, not INTO
		out.push_back(std::move(source));
	}
	return out;
}

// ⚠ NOT HERE YET — a source's fields are the server's (query-constructor-doors.md: request Fields). Until then nothing is offered.
std::vector<ibQueryConstructorField> ibQueryConstructorModel::GetFields(const ibQuerySource& /*source*/,
	const ibQueryPackage& /*package*/, size_t /*beforeStatement*/) const
{
	return {};
}

// ⚠ NOT HERE YET — the same fields, qualified (the client qualifies the answer to request Fields, query-constructor-doors.md).
std::vector<ibQueryConstructorField> ibQueryConstructorModel::GetQualifiedFields(const ibQuerySource& /*source*/,
	const ibQueryPackage& /*package*/, size_t /*beforeStatement*/) const
{
	return {};
}

// ⚠ NOT HERE YET — what a reference points at is the configuration's (query-constructor-doors.md, question 2).
std::vector<ibQueryConstructorField> ibQueryConstructorModel::GetReferenceFields(ibClassID /*clsid*/,
	const wxString& /*sourceLabel*/) const
{
	return {};
}

// ⚠ NOT HERE YET — query-constructor-doors.md, question 2.
std::vector<ibQueryConstructorField> ibQueryConstructorModel::GetReferenceFields(const ibTypeDescription& /*typeDesc*/,
	const wxString& /*sourceLabel*/) const
{
	return {};
}

// ⚠ NOT HERE YET — query-constructor-doors.md, question 2.
std::vector<ibQueryConstructorField> ibQueryConstructorModel::GetReferenceBranches(const ibTypeDescription& /*typeDesc*/) const
{
	return {};
}

// THE DESKTOP'S, WHOLE: a primitive is the core's CAST (ibQueryMakeCast); a reference is cast to the one table behind
// it, which GetReferenceBranches names — ⚠ NOT HERE YET there, so a reference is left as it is, as the desktop leaves a
// type no CAST can say.
ibQueryAstExprPtr ibQueryConstructorModel::CastTo(ibQueryAstExprPtr value, const ibTypeDescription& type) const
{
	if (ibQueryAstExprPtr primitive = ibQueryMakeCast(value, type))
		return primitive;
	const std::vector<ibQueryConstructorField> branches =
		type.GetClsidCount() == 1 ? GetReferenceBranches(type) : std::vector<ibQueryConstructorField>();
	if (branches.size() != 1)
		return value;
	auto cast = ibQueryAstExpr::Make(ibQueryAstExprKind::Cast);
	cast->m_arg = std::move(value);
	const wxString& table = branches.front().m_name;   // `Catalog.Goods`
	cast->m_path = { table.BeforeFirst(wxT('.')), table.AfterFirst(wxT('.')) };
	return cast;
}

// A PRIMITIVE CAST IS READ HERE (the core's ibQueryCastType). ⚠ NOT HERE YET: a CAST to a table types its value by that
// table's empty reference, which is the configuration's (read from the answer to request Fields, query-constructor-doors.md) — until then it is "unknown", an empty type.
ibTypeDescription ibQueryConstructorModel::TypeOfCast(const ibQueryAstExpr& cast) const
{
	ibTypeDescription type;
	if (ibQueryCastType(cast, type))
		return type;
	return ibTypeDescription();
}

// THE DESKTOP'S, WHOLE — the reader's own text says what it takes; the type is the CAST's (TypeOfCast above). ⚠ NOT HERE
// YET: whether that type is a walkable reference is the configuration's (read from the answer to request Fields, query-constructor-doors.md), so no [+] is offered on it.
std::vector<ibQueryConstructorField> ibQueryConstructorModel::FieldsTakenFrom(const ibQuerySelect& reader,
	const ibQuerySource& source) const
{
	const wxString table = ibQuerySourceName(source);
	std::vector<ibQueryConstructorField> out;
	for (const ibQueryProjection& projection : reader.m_projections) {
		const ibQueryAstExprPtr& e = projection.m_expr;
		const bool cast = e && e->m_kind == ibQueryAstExprKind::Cast;
		const ibQueryAstExprPtr& column = cast ? e->m_arg : e;
		if (!column || column->m_kind != ibQueryAstExprKind::Column || column->m_arg || column->m_path.size() != 2
		    || !column->m_path.front().IsSameAs(table, false))
			continue;
		const wxString& name = column->m_path.back();
		if (std::any_of(out.begin(), out.end(), [&name](const ibQueryConstructorField& f) { return f.m_name.IsSameAs(name, false); }))
			continue;
		ibQueryConstructorField field;
		field.m_name = field.m_presentation = name;
		field.m_source = ibQuerySourceLabel(source);
		if (cast)
			field.m_type = TypeOfCast(*e);
		out.push_back(std::move(field));
	}
	return out;
}

// ⚠ NOT HERE YET — read from request Fields (query-constructor-doors.md). An unresolvable path is an EMPTY field — silence, as on the desktop.
ibQueryConstructorField ibQueryConstructorModel::FieldOfPath(const ibQuerySelect& /*select*/,
	const std::vector<wxString>& /*path*/, const ibQueryPackage& /*package*/, size_t /*beforeStatement*/) const
{
	return ibQueryConstructorField();
}

// ⚠ NOT HERE YET — read from request Fields (query-constructor-doors.md, question 2).
ibQueryConstructorField ibQueryConstructorModel::WalkFrom(const ibQueryConstructorField& /*from*/,
	const std::vector<wxString>& /*hops*/) const
{
	return ibQueryConstructorField();
}

// ⚠ NOT HERE YET — read from request Fields (query-constructor-doors.md, question 2).
ibQueryConstructorField ibQueryConstructorModel::WalkPath(const std::vector<ibQueryConstructorField>& /*fields*/,
	const std::vector<wxString>& /*path*/) const
{
	return ibQueryConstructorField();
}

// ⚠ NOT HERE YET — read from request Fields (query-constructor-doors.md, question 2).
ibClassID ibQueryConstructorModel::ReferenceOfPath(const ibQuerySelect& /*select*/, const std::vector<wxString>& /*path*/,
	const ibQueryPackage& /*package*/, size_t /*beforeStatement*/) const
{
	return 0;
}

// ⚠ NOT HERE YET — read from request Fields (query-constructor-doors.md). Empty is "unknown", never "no type".
ibTypeDescription ibQueryConstructorModel::TypeOfPath(const ibQuerySelect& /*select*/, const std::vector<wxString>& /*path*/,
	const ibQueryPackage& /*package*/, size_t /*beforeStatement*/) const
{
	return ibTypeDescription();
}

// ⚠ NOT HERE YET — a projection's type is resolved against the configuration (read from the answer to request Fields, query-constructor-doors.md).
std::vector<ibQueryConstructorField> ibQueryConstructorModel::FieldsOfSelect(const ibQuerySelect& /*select*/,
	const ibQueryPackage& /*package*/, size_t /*beforeStatement*/) const
{
	return {};
}

//////////////////////////////////////////////////////////////////////
// Roles — the package's own word, read here as on the desktop
//////////////////////////////////////////////////////////////////////

namespace {

// EVERY ROLE A SELECT SAYS, FORGOTTEN — its own projections, its union's branches and what it reads FROM.
// True when there was one.
bool ForgetSaidRoles(ibQuerySelect& select)
{
	bool forgot = false;
	for (ibQueryProjection& projection : select.m_projections) {
		if (!projection.m_roleSaid)
			continue;
		projection.m_roleSaid   = false;
		projection.m_role       = ibBalanceRole::None;
		projection.m_periodRank = 0;
		forgot = true;
	}
	if (select.m_from.m_subquery)
		forgot = ForgetSaidRoles(*select.m_from.m_subquery) || forgot;
	for (ibQueryAstJoin& join : select.m_joins)
		if (join.m_source.m_subquery)
			forgot = ForgetSaidRoles(*join.m_source.m_subquery) || forgot;
	for (const ibQuerySelectPtr& branch : select.m_unions)
		if (branch)
			forgot = ForgetSaidRoles(*branch) || forgot;
	return forgot;
}

} // namespace

bool ibQueryDropRoles(ibQueryPackage& package)
{
	bool forgot = false;
	for (ibQueryAstStatement& statement : package.m_statements)
		if (statement.m_select)
			forgot = ForgetSaidRoles(*statement.m_select) || forgot;
	return forgot;
}

wxString ibQueryTextWithoutRoles(const wxString& text)
{
	if (text.IsEmpty())
		return text;
	ibQueryPackage package;
	try { package = ibQueryParser().ParsePackage(text); }
	catch (const ibCoreException&) { return text; }   // half-typed: whoever reads it says why, in the parser's words
	return ibQueryDropRoles(package) ? ibRenderQueryPackage(package) : text;
}
