#include "queryConstructorModel.h"

#include "frmclient/backend/metaCollection/partial/reference/reference.h"   // ibValueReferenceDataObject — the fields' [+]
#include "frmclient/docView/tabDocument.h"                                  // a tab's fetch — the fields of its text
#include "frmclient/mainFrame/mainFrame.h"
#include "frmclient/win/dlgs/settings/settingsFieldTree.h"   // ibReadSettingsFields — a field as the server writes it
#include "protocol/protocol.h"
#include "core/query/queryParser.h"   // ibQueryParser — the package is read here, as the desktop reads it
#include "core/query/queryRender.h"   // ibRenderQueryPackage — …and written back
#include "core/clsid.h"               // IsReference — what a field can be walked through
#include "core/guid.h"                // a configuration picture's name
#include "core/serialize/dataBuilder.h"   // a type, as the server wrote it
#include "frmclient/backend/serialize/dataProtocol.h"   // ibReadProtocolNode — …read off the answer

#include <algorithm>
#include <functional>

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

namespace qc = ibProtocol::ibView::ibQueryConstructor;

// WHAT THE TAB ALREADY ANSWERED, for the model's life: the sources once, each source's fields and parameters once, a
// reference's fields once per target. The window asks at every edit; the server is asked once per question.
struct ibQueryModelAnswers {
	bool                                   m_sourcesAsked = false;
	ibProtocolNode                         m_sources;
	std::map<wxString, ibProtocolNode>     m_fields;       // by the source as written (and its slot)
	std::map<wxString, ibProtocolNode>     m_parameters;   // by the source as written
	std::map<ibMetaID, ibProtocolNode>     m_expanded;     // by a reference's target
};

namespace {

// ⚠ WHAT CAN BE WALKED THROUGH — the desktop's two rules (queryConstructorModel.cpp), word for word: a type with one
// reference REFERS to it; a type with any reference CAN be walked.
ibClassID SingleReferenceOf(const std::vector<ibClassID>& clsids)
{
	return clsids.size() == 1 && IsReference(clsids.front()) && !clsid_is_any(clsids.front()) ? clsids.front() : 0;
}

bool HasReference(const std::vector<ibClassID>& clsids)
{
	for (const ibClassID& clsid : clsids)
		if (IsReference(clsid) && !clsid_is_any(clsid))
			return true;
	return false;
}

// A FIELD'S PICTURE AS THE SERVER NAMED IT (ibBackendPicture::SendPicture): an engine picture by its number, a
// configuration's by its guid. A picture sent as its data has no name to keep — the drawer's default.
ibPictureDescription PictureOf(const wxString& sent)
{
	unsigned long long id = 0;
	if (!sent.IsEmpty() && sent.ToULongLong(&id))
		return ibPictureDescription(static_cast<ibPictureID>(id));
	const ibGuid guid(sent);
	if (!sent.IsEmpty() && guid.isValid())
		return ibPictureDescription(guid);
	return ibPictureDescription();
}

// ONE FIELD AS THE DESKTOP'S FieldOfExplorer MAKES IT — from a row the server wrote as a settings field tree reads it.
ibQueryConstructorField FieldOfRow(ibSettingsPlainField&& row, const wxString& sourceLabel)
{
	ibQueryConstructorField field;
	field.m_name           = row.m_name;
	field.m_presentation   = row.m_name;   // the constructor speaks the query's language — the name
	field.m_caption        = row.m_presentation;
	field.m_source         = sourceLabel;
	field.m_referenceClsid = SingleReferenceOf(row.m_type.GetClsidList());
	field.m_reference      = HasReference(row.m_type.GetClsidList());
	field.m_type           = std::move(row.m_type);
	field.m_picture        = PictureOf(row.m_picture);
	field.m_available      = row.m_available;
	return field;
}

std::vector<ibQueryConstructorField> FieldsOfRows(const ibProtocolNode& answer, const char* rows, const char* references,
	const wxString& sourceLabel)
{
	ibValueReferenceDataObject::ReadReferences(answer.FindChild(references));
	std::vector<ibQueryConstructorField> fields;
	for (ibSettingsPlainField& row : ibReadSettingsFields(answer.FindChild(rows)))
		fields.push_back(FieldOfRow(std::move(row), sourceLabel));
	return fields;
}

// ASKED OF THE TAB THE WINDOW WAS OPENED FROM — the configuration's. Off a tab, nobody to ask: no answer.
bool AskTab(const ibMetaData* metaData, const ibProtocolNode& request, ibProtocolNode& answer)
{
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (frame == nullptr || metaData == nullptr || metaData->GetTabId() == 0)
		return false;
	const ibTabDocument* const document = frame->FindDocument(metaData->GetTabId());
	return document != nullptr && document->Fetch(request, answer);
}

// THE SOURCE AS A FROM WRITES IT, its arguments too — what decides its columns (a turnover's periodicity), and the key
// its answer is kept by. No alias: the fields are the table's.
wxString SourceText(const ibQuerySource& source)
{
	wxString text;
	for (size_t i = 0; i < source.m_name.size(); ++i)
		text += (i > 0 ? wxT(".") : wxT("")) + source.m_name[i];
	if (!source.m_args.empty()) {
		text += wxT("(");
		for (size_t i = 0; i < source.m_args.size(); ++i) {
			if (i > 0)
				text += wxT(", ");
			if (source.m_args[i])
				text += ibRenderQueryExpr(*source.m_args[i]);
		}
		text += wxT(")");
	}
	return text;
}

} // namespace

ibQueryConstructorModel::ibQueryConstructorModel(const ibMetaData* metaData)
	: m_metaData(metaData), m_known(std::make_shared<ibQueryModelAnswers>())
{
}

// THE WALK IS THE SERVER'S — the desktop walks its factory's descriptors; the tab answers with that walk, sorted
// (request Sources).
std::vector<ibQueryConstructorSource> ibQueryConstructorModel::GetSources() const
{
	if (!m_known->m_sourcesAsked) {
		m_known->m_sourcesAsked = true;
		ibProtocolNode request;
		request.SetValue(qc::Kind, static_cast<long long>(qc::ibRequest::Sources));
		if (!AskTab(m_metaData, request, m_known->m_sources))
			m_known->m_sources = ibProtocolNode();
	}
	std::vector<ibQueryConstructorSource> out;
	for (const ibProtocolNode& row : m_known->m_sources.GetList(qc::Sources)) {
		ibQueryConstructorSource source;
		for (const ibProtocolNode& segment : row.GetList(qc::Path))
			source.m_path.push_back(segment.AsString());
		source.m_presentation = row.GetString(qc::Presentation);
		if (!source.m_path.empty())
			out.push_back(std::move(source));
	}
	return out;
}

namespace {

// …AND A TABLE'S OWN REFERENCE TYPE, from the same answer — what a CAST to it brings a value to, and what names a
// reference's branch. 0: none (a register, a virtual table).
ibClassID ReferenceOfSource(const ibProtocolNode& sources, const std::vector<wxString>& path)
{
	for (const ibProtocolNode& row : sources.GetList(qc::Sources)) {
		const std::vector<ibProtocolNode> segments = row.GetList(qc::Path);
		if (segments.size() != path.size())
			continue;
		bool same = true;
		for (size_t i = 0; i < path.size() && same; ++i)
			same = segments[i].AsString().IsSameAs(path[i], false);
		unsigned long long clsid = 0;
		if (same && row.GetString(qc::Reference).ToULongLong(&clsid))
			return static_cast<ibClassID>(clsid);
	}
	return 0;
}

// …and the table a reference type names: its path joined, as GetReferenceBranches names it.
wxString SourceOfReference(const ibProtocolNode& sources, ibClassID clsid)
{
	for (const ibProtocolNode& row : sources.GetList(qc::Sources)) {
		unsigned long long reference = 0;
		if (!row.GetString(qc::Reference).ToULongLong(&reference) || static_cast<ibClassID>(reference) != clsid)
			continue;
		wxString name;
		for (const ibProtocolNode& segment : row.GetList(qc::Path))
			name += (name.IsEmpty() ? wxT("") : wxT(".")) + segment.AsString();
		return name;
	}
	return wxString();
}

} // namespace

// THE DESCRIPTOR'S ANSWER, asked of the tab (request SourceParameters) — a source with fewer than two segments, or a
// nested one, has no descriptor on the desktop either.
std::vector<ibQuerySourceParameter> ibQueryConstructorModel::GetSourceParameters(const ibQuerySource& source) const
{
	std::vector<ibQuerySourceParameter> out;
	if (source.m_name.size() < 2 || source.m_subquery)
		return out;
	const wxString text = SourceText(source);
	auto known = m_known->m_parameters.find(text);
	if (known == m_known->m_parameters.end()) {
		ibProtocolNode request, answer;
		request.SetValue(qc::Kind, static_cast<long long>(qc::ibRequest::SourceParameters)).SetValue(qc::Source, text);
		if (!AskTab(m_metaData, request, answer))
			answer = ibProtocolNode();
		known = m_known->m_parameters.emplace(text, answer).first;
	}
	for (const ibProtocolNode& row : known->second.GetList(qc::Parameters)) {
		ibQuerySourceParameter parameter;
		parameter.m_name = row.GetString(qc::Name);
		parameter.m_condition = row.GetBool(qc::Condition);
		auto type = std::make_shared<ibDataNode>();
		ibReadProtocolNode(row.FindChild(qc::Type), *type);
		ibTypeDescriptionMemory::ReadNode(ibDataValue::Child(type), parameter.m_type);
		parameter.m_required = row.GetBool(qc::Required);
		for (const ibProtocolNode& choice : row.GetList(qc::Choices))
			parameter.m_choices.push_back(choice.AsString());
		parameter.m_description = row.GetString(qc::Description);
		parameter.m_consumedBySource = row.GetBool(qc::ConsumedBySource);
		parameter.m_default = row.GetString(qc::Default);
		out.push_back(std::move(parameter));
	}
	return out;
}

// THE CONDITION'S OWN SET (the descriptor's FillConditionExplorer) — request Fields with the slot.
std::vector<ibQueryConstructorField> ibQueryConstructorModel::GetConditionFields(const ibQuerySource& source,
	const wxString& slot) const
{
	if (source.m_name.size() < 2 || source.m_subquery)
		return {};
	const wxString text = SourceText(source);
	const wxString key = text + wxT("\n") + slot + wxT("\nslot");
	auto known = m_known->m_fields.find(key);
	if (known == m_known->m_fields.end()) {
		ibProtocolNode request, answer;
		request.SetValue(qc::Kind, static_cast<long long>(qc::ibRequest::Fields)).SetValue(qc::Source, text)
			.SetValue(qc::Slot, slot);
		if (!AskTab(m_metaData, request, answer))
			answer = ibProtocolNode();
		known = m_known->m_fields.emplace(key, answer).first;
	}
	return FieldsOfRows(known->second, qc::Fields, qc::References, wxString());
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

namespace {

// …AND A FIELD WHOSE TYPE A CAST SAYS — typed and walkable the same way, from the type the CAST names (the desktop's).
void TypeByCast(const ibQueryConstructorModel& model, const ibQueryAstExpr& cast, ibQueryConstructorField& field)
{
	field.m_type           = model.TypeOfCast(cast);
	field.m_referenceClsid = SingleReferenceOf(field.m_type.GetClsidList());
	field.m_reference      = HasReference(field.m_type.GetClsidList());
}

} // namespace

// THE WALK, DONE ONCE — the desktop's, whole.
ibQueryConstructorField ibQueryConstructorModel::FieldOfPath(const ibQuerySelect& select,
                                                             const std::vector<wxString>& path,
                                                             const ibQueryPackage& package,
                                                             size_t beforeStatement) const
{
	if (path.empty())
		return ibQueryConstructorField();

	const ibQuerySource* source = nullptr;
	size_t first = 0;
	if (path.size() > 1) {
		if (ibQuerySourceName(select.m_from).IsSameAs(path[0], false)) {
			source = &select.m_from;
			first  = 1;
		}
		else {
			for (const ibQueryAstJoin& join : select.m_joins)
				if (ibQuerySourceName(join.m_source).IsSameAs(path[0], false)) {
					source = &join.m_source;
					first  = 1;
					break;
				}
		}
	}

	std::vector<ibQueryConstructorField> fields;
	if (source != nullptr) {
		fields = GetFields(*source, package, beforeStatement);
	}
	else {
		std::vector<const ibQuerySource*> all{ &select.m_from };
		for (const ibQueryAstJoin& join : select.m_joins)
			all.push_back(&join.m_source);
		for (const ibQuerySource* candidate : all) {
			std::vector<ibQueryConstructorField> its = GetFields(*candidate, package, beforeStatement);
			for (const ibQueryConstructorField& field : its)
				if (field.m_name.IsSameAs(path[0], false)) { fields = std::move(its); break; }
			if (!fields.empty())
				break;
		}
	}

	return WalkPath(fields, std::vector<wxString>(path.begin() + first, path.end()));
}

ibQueryConstructorField ibQueryConstructorModel::WalkPath(const std::vector<ibQueryConstructorField>& fields,
                                                         const std::vector<wxString>& path) const
{
	for (size_t head = path.size(); head > 0; --head) {
		wxString name = path.front();
		for (size_t i = 1; i < head; ++i)
			name += wxT(".") + path[i];
		const auto start = std::find_if(fields.begin(), fields.end(),
			[&name](const ibQueryConstructorField& field) { return field.m_name.IsSameAs(name, false); });
		if (start != fields.end())
			return WalkFrom(*start, std::vector<wxString>(path.begin() + head, path.end()));
	}
	return ibQueryConstructorField();   // a name this model cannot resolve — silence, never a guess
}

ibQueryConstructorField ibQueryConstructorModel::WalkFrom(const ibQueryConstructorField& from,
                                                         const std::vector<wxString>& hops) const
{
	ibQueryConstructorField leaf = from;
	for (const wxString& hop : hops) {
		const std::vector<ibQueryConstructorField> fields = GetReferenceFields(leaf.m_type);
		const auto found = std::find_if(fields.begin(), fields.end(),
			[&hop](const ibQueryConstructorField& field) { return field.m_name.IsSameAs(hop, false); });
		if (found == fields.end()) {
			ibQueryConstructorField unresolved;
			unresolved.m_available = leaf.m_available;
			return unresolved;
		}
		const bool available = leaf.m_available && found->m_available;   // one hidden hop hides the walk
		leaf = *found;
		leaf.m_available = available;
	}
	return leaf;
}

ibClassID ibQueryConstructorModel::ReferenceOfPath(const ibQuerySelect& select, const std::vector<wxString>& path,
	const ibQueryPackage& package, size_t beforeStatement) const
{
	return FieldOfPath(select, path, package, beforeStatement).m_referenceClsid;
}

ibTypeDescription ibQueryConstructorModel::TypeOfPath(const ibQuerySelect& select, const std::vector<wxString>& path,
	const ibQueryPackage& package, size_t beforeStatement) const
{
	// EMPTY IS "UNKNOWN", never "no type".
	return FieldOfPath(select, path, package, beforeStatement).m_type;
}

// WHAT A SELECT PRODUCES, as fields — the desktop's, whole.
std::vector<ibQueryConstructorField> ibQueryConstructorModel::FieldsOfSelect(
	const ibQuerySelect& select, const ibQueryPackage& package, size_t beforeStatement) const
{
	std::vector<ibQueryConstructorField> out;

	if (select.m_selectAll)
		return GetFields(select.m_from, package, beforeStatement);

	for (const ibQueryProjection& projection : select.m_projections) {
		ibQueryConstructorField field;
		field.m_name = ibQueryOutputName(projection);
		if (field.m_name.IsEmpty() && projection.m_expr)
			field.m_name = ibRenderQueryExpr(*projection.m_expr);
		if (field.m_name.IsEmpty())
			continue;
		field.m_presentation = field.m_name;

		if (projection.m_expr && projection.m_expr->m_kind == ibQueryAstExprKind::Column) {
			const ibQueryConstructorField of = FieldOfPath(select, projection.m_expr->m_path, package, beforeStatement);
			field.m_referenceClsid = of.m_referenceClsid;
			field.m_reference      = of.m_reference;
			field.m_type           = of.m_type;
			if (!of.m_picture.IsEmptyPicture())
				field.m_picture = of.m_picture;
			field.m_available = of.m_available;
			field.m_balanceRole = of.m_balanceRole;
			if (!projection.m_expr->m_path.empty() && field.m_name.IsSameAs(projection.m_expr->m_path.back(), false))
				field.m_caption = of.m_caption;
		}
		else if (projection.m_expr) {
			std::function<bool(const ibQueryAstExpr&)> readsAvailable = [&](const ibQueryAstExpr& e) {
				if (e.m_kind == ibQueryAstExprKind::Column)
					return FieldOfPath(select, e.m_path, package, beforeStatement).m_available;
				bool available = true;
				ibQueryForEachOperand(e, [&](const ibQueryAstExprPtr& child) {
					available = available && (!child || readsAvailable(*child));
				});
				return available;
			};
			field.m_available = readsAvailable(*projection.m_expr);
			if (projection.m_expr->m_kind == ibQueryAstExprKind::Cast)
				TypeByCast(*this, *projection.m_expr, field);
		}
		if (projection.m_roleSaid)
			field.m_balanceRole = projection.m_role;
		out.push_back(std::move(field));
	}
	return out;
}

// THE FIELDS OF A SOURCE — the desktop's, whole; only a REAL source's are the server's (request Fields).
std::vector<ibQueryConstructorField> ibQueryConstructorModel::GetFields(
	const ibQuerySource& source, const ibQueryPackage& package, size_t beforeStatement) const
{
	struct Stamp {
		const wxString label;
		std::vector<ibQueryConstructorField> operator()(std::vector<ibQueryConstructorField> fields) const {
			for (ibQueryConstructorField& field : fields) field.m_source = label;
			return fields;
		}
	} stamp{ ibQuerySourceLabel(source) };

	if (source.m_subquery)
		return stamp(FieldsOfSelect(*source.m_subquery, package, beforeStatement));

	if (source.m_name.empty())
		return {};

	auto described = [&]() -> std::vector<ibQueryConstructorField> {
		auto reads = [&source](const ibQuerySelect& select) {
			if (&select.m_from == &source)
				return true;
			for (const ibQueryAstJoin& join : select.m_joins)
				if (&join.m_source == &source)
					return true;
			return false;
		};
		for (const ibQueryAstStatement& statement : package.m_statements) {
			if (!statement.m_select)
				continue;
			if (reads(*statement.m_select))
				return stamp(FieldsTakenFrom(*statement.m_select, source));
			for (const ibQuerySelectPtr& branch : statement.m_select->m_unions)
				if (branch && reads(*branch))
					return stamp(FieldsTakenFrom(*branch, source));
		}
		return {};
	};
	if (source.m_parameter)
		return described();

	if (source.m_name.size() == 1) {
		const size_t limit = std::min(beforeStatement, package.m_statements.size());
		for (size_t i = limit; i > 0; --i) {
			const ibQueryAstStatement& statement = package.m_statements[i - 1];
			if (statement.m_select
			    && (statement.m_select->m_intoTemp.IsSameAs(source.m_name[0], false)
			     || statement.m_select->m_ontoName.IsSameAs(source.m_name[0], false)))
				return stamp(FieldsOfSelect(*statement.m_select, package, i - 1));
		}
		return described();
	}

	// A REAL SOURCE answers through its descriptor, the server's — asked with its arguments, which decide its columns.
	const wxString text = SourceText(source);
	auto known = m_known->m_fields.find(text);
	if (known == m_known->m_fields.end()) {
		ibProtocolNode request, answer;
		request.SetValue(qc::Kind, static_cast<long long>(qc::ibRequest::Fields)).SetValue(qc::Source, text);
		if (!AskTab(m_metaData, request, answer))
			answer = ibProtocolNode();
		known = m_known->m_fields.emplace(text, answer).first;
	}
	return stamp(FieldsOfRows(known->second, qc::Fields, qc::References, wxString()));
}

std::vector<ibQueryConstructorField> ibQueryConstructorModel::GetQualifiedFields(
	const ibQuerySource& source, const ibQueryPackage& package, size_t beforeStatement) const
{
	const wxString prefix = ibQuerySourceName(source);

	std::vector<ibQueryConstructorField> out = GetFields(source, package, beforeStatement);
	if (prefix.IsEmpty())
		return out;

	// ⚠ ONLY THE NAME IS QUALIFIED — never the PRESENTATION (the desktop's).
	for (ibQueryConstructorField& field : out)
		field.m_name = prefix + wxT(".") + field.m_name;
	return out;
}

// ONE LEVEL DOWN A REFERENCE — the composer's Expand, asked of the same tab with the reference's target (what the
// desktop's factory resolves by the clsid's metaID).
std::vector<ibQueryConstructorField> ibQueryConstructorModel::GetReferenceFields(ibClassID clsid,
	const wxString& sourceLabel) const
{
	if (clsid == 0 || !IsReference(clsid) || clsid_is_any(clsid))
		return {};   // not a reference, or a barrier (`AnyRef`, `CatalogRef`): no one table is behind it

	const ibMetaID target = static_cast<ibMetaID>(clsid_metaID(clsid));
	auto known = m_known->m_expanded.find(target);
	if (known == m_known->m_expanded.end()) {
		ibProtocolNode request, answer;
		request.SetValue(ibProtocol::ibView::ibComposer::Kind,
			static_cast<long long>(ibProtocol::ibView::ibComposer::ibRequest::Expand));
		request.AddItem(ibProtocol::ibView::ibComposer::Targets, static_cast<long long>(target));
		if (!AskTab(m_metaData, request, answer))
			answer = ibProtocolNode();
		known = m_known->m_expanded.emplace(target, answer).first;
	}
	return FieldsOfRows(known->second, ibProtocol::ibView::ibComposer::Expanded, ibProtocol::ibView::ibComposer::References,
		sourceLabel);
}

// EVERY ALTERNATIVE OF A TYPE, MERGED BY NAME — the desktop's, whole.
std::vector<ibQueryConstructorField> ibQueryConstructorModel::GetReferenceFields(const ibTypeDescription& typeDesc,
	const wxString& sourceLabel) const
{
	std::vector<ibQueryConstructorField> out;

	for (const ibClassID& clsid : typeDesc.GetClsidList()) {
		if (!IsReference(clsid))
			continue;

		for (ibQueryConstructorField& field : GetReferenceFields(clsid, sourceLabel)) {
			const auto same = std::find_if(out.begin(), out.end(),
				[&field](const ibQueryConstructorField& have) { return have.m_name.IsSameAs(field.m_name, false); });
			if (same == out.end()) {
				out.push_back(std::move(field));
				continue;
			}
			for (const ibClassID& alternative : field.m_type.GetClsidList())
				same->m_type.AppendMetaType(alternative);
			same->m_reference = HasReference(same->m_type.GetClsidList());
			same->m_referenceClsid = SingleReferenceOf(same->m_type.GetClsidList());
			same->m_available = same->m_available || field.m_available;
		}
	}
	return out;
}

// ONE ENTRY PER ALTERNATIVE, named as a query names the type — the table whose own reference it is (request Sources).
std::vector<ibQueryConstructorField> ibQueryConstructorModel::GetReferenceBranches(const ibTypeDescription& typeDesc) const
{
	std::vector<ibQueryConstructorField> out;
	GetSources();   // the answer the names are read from
	for (const ibClassID& clsid : typeDesc.GetClsidList()) {
		if (!IsReference(clsid) || clsid_is_any(clsid))
			continue;
		const wxString name = SourceOfReference(m_known->m_sources, clsid);
		if (name.IsEmpty())
			continue;   // a type with no queryable (a report, a data processor) — not a table
		ibQueryConstructorField branch;
		branch.m_name           = name;
		branch.m_presentation   = name;
		branch.m_referenceClsid = clsid;
		branch.m_reference      = true;
		branch.m_type.SetDefaultMetaType(clsid);
		out.push_back(std::move(branch));
	}
	return out;
}

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

// A PRIMITIVE CAST IS THE CORE'S; a CAST to a table types its value by that table's own reference (request Sources).
ibTypeDescription ibQueryConstructorModel::TypeOfCast(const ibQueryAstExpr& cast) const
{
	ibTypeDescription type;
	if (ibQueryCastType(cast, type))
		return type;
	type = ibTypeDescription();
	if (cast.m_kind != ibQueryAstExprKind::Cast || cast.m_path.size() < 2)
		return type;
	GetSources();
	const ibClassID reference = ReferenceOfSource(m_known->m_sources, cast.m_path);
	if (reference != 0)
		type.SetDefaultMetaType(reference);
	return type;
}

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
			TypeByCast(*this, *e, field);
		out.push_back(std::move(field));
	}
	return out;
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
