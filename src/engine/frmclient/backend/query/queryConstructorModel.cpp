#include "queryConstructorModel.h"

#include "frmclient/backend/metaCollection/partial/reference/reference.h"   // ibValueReferenceDataObject — the fields' [+]
#include "frmclient/docView/tabDocument.h"                                  // a tab's fetch — the fields of its text
#include "frmclient/mainFrame/mainFrame.h"
#include "frmclient/win/dlgs/settings/settingsFieldTree.h"   // ibReadSettingsFields — a field as the server writes it
#include "protocol/protocol.h"
#include "core/query/queryRender.h"   // ibRenderQueryPackage — the package, as the server is asked about it
#include "core/exception.h"           // ibCoreException — the engine's verdict, in its words
#include "core/guid.h"                // a configuration picture's name
#include "core/serialize/dataBuilder.h"   // a type, as the server wrote it
#include "frmclient/backend/serialize/dataProtocol.h"   // ibReadProtocolNode — …read off the answer

#include <map>

namespace {

// WHAT A TAB'S VIEW ALREADY SAID OF A TEXT — its query's fields, kept by the tab's id: the panel reads them as it is
// built and again as each tree is filled, and the same text asks the server once.
struct ibKnownQueryFields {
	ibString       m_text;
	ibProtocolNode m_answer;
};

// …and a dynamic list's, by its main table and text.
struct ibKnownListFields {
	ibMetaID       m_mainTable = wxNOT_FOUND;
	ibString       m_text;
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

void ibQueryFieldsKnown(long long tabId, const ibString& text, const ibProtocolNode& answer)
{
	KnownQueryFields()[tabId] = ibKnownQueryFields{ text, answer };
}

void ibQueryFieldsForget(long long tabId)
{
	KnownQueryFields().erase(tabId);
	KnownListFields().erase(tabId);
}

std::vector<ibSettingsPlainField> ibListSourceFields(ibMetaID mainTable, const ibString& text, const ibMetaData* metaData,
	ibString* error)
{
	if (error != nullptr)
		error->clear();

	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (frame == nullptr || metaData == nullptr || metaData->GetTabId() == 0)
		return {};
	if (mainTable == wxNOT_FOUND && ibString(text).Trim(true).Trim(false).IsEmpty())
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

std::vector<ibQueryConstructorField> ibQueryFieldsOfText(const ibString& text, const ibMetaData* metaData, ibString* error)
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
		if (ibString(text).Trim(true).Trim(false).IsEmpty())
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
	std::map<ibString, ibProtocolNode>     m_fields;       // by the source as written (and its slot)
	std::map<ibString, ibProtocolNode>     m_parameters;   // by the source as written
	std::map<ibMetaID, ibProtocolNode>     m_expanded;     // by a reference's target
};

namespace {

// A FIELD'S PICTURE AS THE SERVER NAMED IT (ibBackendPicture::SendPicture): an engine picture by its number, a
// configuration's by its guid. A picture sent as its PNG has no name to keep — the drawer's default.
ibPictureDescription PictureOf(const ibDataValue& picture)
{
	const ibString sent = picture.Kind() == ibDataKind::String ? ibString(picture.AsString()) : ibString();
	unsigned long long id = 0;
	if (!sent.IsEmpty() && sent.ToULongLong(&id))
		return ibPictureDescription(static_cast<ibPictureID>(id));
	const ibGuid guid(sent);
	if (!sent.IsEmpty() && guid.isValid())
		return ibPictureDescription(guid);
	return ibPictureDescription();
}

// ONE FIELD AS THE DESKTOP'S FieldOfExplorer MAKES IT — from a row the server wrote as a settings field tree reads it;
// what its type refers to, asked of the references the server described with the same answer.
ibQueryConstructorField FieldOfRow(const ibQueryConstructorModel& model, ibSettingsPlainField&& row)
{
	ibQueryConstructorField field;
	field.m_name           = row.m_name;
	field.m_presentation   = row.m_name;   // the constructor speaks the query's language — the name
	field.m_caption        = row.m_presentation;
	field.m_type           = std::move(row.m_type);
	model.ResolveReference(field);
	field.m_picture        = PictureOf(row.m_picture);
	field.m_available      = row.m_available;
	return field;
}

std::vector<ibQueryConstructorField> FieldsOfRows(const ibQueryConstructorModel& model, const ibProtocolNode& answer,
	const char* rows, const char* references)
{
	ibValueReferenceDataObject::ReadReferences(answer.FindChild(references));
	std::vector<ibQueryConstructorField> fields;
	for (ibSettingsPlainField& row : ibReadSettingsFields(answer.FindChild(rows)))
		fields.push_back(FieldOfRow(model, std::move(row)));
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
ibString SourceText(const ibQuerySource& source)
{
	ibString text;
	for (size_t i = 0; i < source.m_name.size(); ++i)
		text += (i > 0 ? ibT(".") : ibT("")) + source.m_name[i];
	if (!source.m_args.empty()) {
		text += ibT("(");
		for (size_t i = 0; i < source.m_args.size(); ++i) {
			if (i > 0)
				text += ibT(", ");
			if (source.m_args[i])
				text += ibRenderQueryExpr(*source.m_args[i]);
		}
		text += ibT(")");
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
	for (const ibProtocolNode& row : m_known->m_sources.FindChild(qc::Sources).Children()) {
		ibQueryConstructorSource source;
		for (const ibProtocolNode& segment : row.GetList(qc::Path))
			source.m_path.push_back(segment.AsString());
		source.m_presentation = row.GetString(qc::Presentation);
		if (!source.m_path.empty())
			out.push_back(std::move(source));
	}
	return out;
}

// THE KIND'S ROW of the same answer (request Sources) — its picture is the kind's, and every table of it wears it.
ibString ibQueryConstructorModel::KindPicture(const ibString& kind) const
{
	GetSources();   // asked once, for the model's life
	for (const ibProtocolNode& row : m_known->m_sources.FindChild(qc::Kinds).Children())
		if (row.GetString(qc::Name).IsSameAs(kind, false))
			return row.GetString(qc::Picture);
	return ibString();
}

// THE DESKTOP'S ibQueryLowering::CheckNames, asked of the tab with the whole text (request Check) each time the window
// asks it. Off a tab, nobody to ask: silence — the desktop's answer for what it cannot verify.
void ibQueryConstructorModel::CheckNames(const ibQueryPackage& package) const
{
	ibProtocolNode request, answer;
	request.SetValue(qc::Kind, static_cast<long long>(qc::ibRequest::Check))
		.SetValue(qc::Text, ibRenderQueryPackage(package));
	if (!AskTab(m_metaData, request, answer))
		return;
	const ibString error = answer.GetString(qc::Error);
	if (!error.IsEmpty())
		ibCoreException::Error(ibT("%s"), error);
}

// …AND ITS UngroupedProjections, of the select the window is on — the same request, with the select named.
std::vector<ibQueryAstExprPtr> ibQueryConstructorModel::UngroupedProjections(const ibQueryPackage& package,
	size_t statement, int unionBranch) const
{
	ibProtocolNode request, answer;
	request.SetValue(qc::Kind, static_cast<long long>(qc::ibRequest::Check))
		.SetValue(qc::Text, ibRenderQueryPackage(package))
		.SetValue(qc::Statement, static_cast<long long>(statement))
		.SetValue(qc::UnionBranch, static_cast<long long>(unionBranch));
	std::vector<ibQueryAstExprPtr> out;
	if (!AskTab(m_metaData, request, answer))
		return out;
	for (const ibProtocolNode& missing : answer.GetList(qc::UngroupedProjections))
		if (ibQueryAstExprPtr column = ibQueryColumnFromPath(missing.AsString()))
			out.push_back(std::move(column));
	return out;
}

// THE DESCRIPTOR'S ANSWER, asked of the tab (request SourceParameters) — a source with fewer than two segments, or a
// nested one, has no descriptor on the desktop either.
std::vector<ibQuerySourceParameter> ibQueryConstructorModel::GetSourceParameters(const ibQuerySource& source) const
{
	std::vector<ibQuerySourceParameter> out;
	if (source.m_name.size() < 2 || source.m_subquery)
		return out;
	const ibString text = SourceText(source);
	auto known = m_known->m_parameters.find(text);
	if (known == m_known->m_parameters.end()) {
		ibProtocolNode request, answer;
		request.SetValue(qc::Kind, static_cast<long long>(qc::ibRequest::SourceParameters)).SetValue(qc::Source, text);
		if (!AskTab(m_metaData, request, answer))
			answer = ibProtocolNode();
		known = m_known->m_parameters.emplace(text, answer).first;
	}
	for (const ibProtocolNode& row : known->second.FindChild(qc::Parameters).Children()) {
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
	const ibString& slot) const
{
	if (source.m_name.size() < 2 || source.m_subquery)
		return {};
	const ibString text = SourceText(source);
	const ibString key = text + ibT("\n") + slot + ibT("\nslot");
	auto known = m_known->m_fields.find(key);
	if (known == m_known->m_fields.end()) {
		ibProtocolNode request, answer;
		request.SetValue(qc::Kind, static_cast<long long>(qc::ibRequest::Fields)).SetValue(qc::Source, text)
			.SetValue(qc::Slot, slot);
		if (!AskTab(m_metaData, request, answer))
			answer = ibProtocolNode();
		known = m_known->m_fields.emplace(key, answer).first;
	}
	return FieldsOfRows(*this, known->second, qc::Fields, qc::References);
}

// …A TABLE'S OWN REFERENCE TYPE, from the catalogue's answer (request Sources) — its row's Reference.
ibClassID ibQueryConstructorModel::ReferenceOfTable(const std::vector<ibString>& path) const
{
	GetSources();   // the answer it is read from
	for (const ibProtocolNode& row : m_known->m_sources.FindChild(qc::Sources).Children()) {
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

// …and the table a reference type names: the row whose Reference it is, its path joined.
ibString ibQueryConstructorModel::TableOfReference(ibClassID reference) const
{
	GetSources();   // the answer it is read from
	for (const ibProtocolNode& row : m_known->m_sources.FindChild(qc::Sources).Children()) {
		unsigned long long clsid = 0;
		if (!row.GetString(qc::Reference).ToULongLong(&clsid) || static_cast<ibClassID>(clsid) != reference)
			continue;
		ibString name;
		for (const ibProtocolNode& segment : row.GetList(qc::Path))
			name += (name.IsEmpty() ? ibT("") : ibT(".")) + segment.AsString();
		return name;
	}
	return ibString();
}

// A REAL SOURCE answers through its descriptor, the server's — asked with its arguments, which decide its columns.
std::vector<ibQueryConstructorField> ibQueryConstructorModel::GetTableFields(const ibQuerySource& source) const
{
	const ibString text = SourceText(source);
	auto known = m_known->m_fields.find(text);
	if (known == m_known->m_fields.end()) {
		ibProtocolNode request, answer;
		request.SetValue(qc::Kind, static_cast<long long>(qc::ibRequest::Fields)).SetValue(qc::Source, text);
		if (!AskTab(m_metaData, request, answer))
			answer = ibProtocolNode();
		known = m_known->m_fields.emplace(text, answer).first;
	}
	return FieldsOfRows(*this, known->second, qc::Fields, qc::References);
}

// ONE LEVEL DOWN A REFERENCE — the composer's Expand, asked of the same tab with the table (what the desktop's factory
// resolves by its id).
std::vector<ibQueryConstructorField> ibQueryConstructorModel::GetTableFields(ibMetaID table) const
{
	auto known = m_known->m_expanded.find(table);
	if (known == m_known->m_expanded.end()) {
		ibProtocolNode request, answer;
		request.SetValue(ibProtocol::ibView::ibComposer::Kind,
			static_cast<long long>(ibProtocol::ibView::ibComposer::ibRequest::Expand));
		request.AddItem(ibProtocol::ibView::ibComposer::Targets, static_cast<long long>(table));
		if (!AskTab(m_metaData, request, answer))
			answer = ibProtocolNode();
		known = m_known->m_expanded.emplace(table, answer).first;
	}
	return FieldsOfRows(*this, known->second, ibProtocol::ibView::ibComposer::Expanded,
		ibProtocol::ibView::ibComposer::References);
}

std::vector<ibMetaID> ibQueryConstructorModel::ConvertToMetaIds(const std::vector<ibClassID>& clsids) const
{
	return ibValueReferenceDataObject::ConvertToMetaIds(clsids, m_metaData);
}
