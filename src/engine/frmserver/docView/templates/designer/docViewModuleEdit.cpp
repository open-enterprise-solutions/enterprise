#include "docViewModuleEdit.h"

#include "backend/metaCollection/metaModuleObject.h"
#include "backend/compiler/compileCode.h"   // ibCompileCode::GetCodeStyle — the syntax the session writes in
#include "backend/compiler/compileModule.h"
#include "backend/metaData.h"   // GetCompileCache — where the module is compiled to be checked
#include "backend/moduleInfo.h"
#include "backend/system/systemManager.h"   // ibValueSystemFunction::Message — "no errors", to the output
#include "backend/backend_mainFrame.h"   // ibBackendDocFrame::ShowModalMessage — the question, to the person
#include "backend/session/session.h"
#include "backend/backend_exception.h"   // ibEvalModeScope — which kind of evaluation a dropdown is
#include "backend/compiler/scriptComplete.h"   // ibValueAtCaret / ibNamesAtCaret — the two answers
#include "backend/objCtor.h"
#include "core/compiler/translateCode.h"   // what the caret stands in
#include "core/compiler/scriptParseCode.h"   // ibContentType — the icon a name wears
#include "core/compiler/blockSyntaxLINQ.h"   // ibLinqBlock::Parse — how much of the module a query opened is
#include "backend/compiler/typeCtor.h"       // ibCtorAbstractType — a kind's class, for its picture
#include "backend/backend_picture.h"         // ibBackendPicture::SendPicture — the picture by its id
#include "core/serialize/dataBuilder.h"
#include "frmserver/client/clientFrame.h"   // IsClosingWindow — the client leaving vs a tab's cross
#include "docViewComposerEdit.h"         // ibFetchComposerRequest — a reference's [+] (Expand), asked by the query constructor
#include "queryConstructorRequest.h"     // ibFetchQueryConstructorRequest — what the query constructor asks the configuration

#include <algorithm>

// ----------------------------------------------------------------------------
// ibModuleTextView implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibModuleTextView, ibMetaView);

void ibModuleTextView::OnDraw(ibDataNode& frame)
{
	const ibValueMetaObjectModuleBase* const creator =
		GetDocument()->ConvertMetaObjectToType<ibValueMetaObjectModuleBase>();
	if (creator == nullptr)
		return;

	// The syntax its words are coloured and completed in (the desktop's editor asked ibCompileCode::GetCodeStyle), and
	// read-only where the configuration may not be edited (ibDOC_READONLY on the desktop's).
	// Added to the State already there — a form editor's frame is its form's, the form's State in it (Child would make
	// a fresh one, and the form's title and command bar went).
	ibDataNode* const drawn = frame.FindChild(ibProtocol::ibView::State);
	ibDataNode& state = drawn != nullptr ? *drawn : frame.Child(ibProtocol::ibView::State);
	state.SetValue(ibProtocol::ibView::ibModule::Syntax, static_cast<std::int32_t>(ibCompileCode::GetCodeStyle()));
	if (!creator->IsEditable())
		state.SetValue(ibProtocol::ibView::ibModule::ReadOnly, true);
	// Whether the configuration the module is in is modified — the document itself never is (ibMetaDocument::IsModified),
	// and Save is offered all along: what the desktop's editor marked on every key the client says once, on the first key
	// while this is false (event Input).
	const ibMetaData* const metaData = creator->GetMetaData();
	state.SetValue(ibProtocol::ibView::ibModule::Modified, metaData != nullptr && metaData->IsModified());
	// The module's address the debugger knows it by — its breakpoints' key, the place a runtime stops at.
	state.SetValue(ibProtocol::ibView::ibModule::Module, creator->GetDocPath());
	// ⭐ THE TEXT RIDES THE FRAME THAT OPENS THE TAB — once: the client draws the module without asking for it (fetch).
	// Only a module's own tab: a form's editor reads its Code page's text when the page is shown.
	if (!m_textDrawn && GetClassInfo() == CLASSINFO(ibModuleTextView)) {
		state.SetValue(ibProtocol::ibView::ibModule::Text, creator->GetModuleText());
		m_textDrawn = true;
	}
}

// ----------------------------------------------------------------------------
// WHAT THE DESKTOP'S CODE EDITOR ASKED THE CONFIGURATION (frontend/win/editor/codeEditor/codeEditorLoader.cpp) — its
// loader's bodies, where the compiler and the configuration are: each a row {Kind, Name, Help} of the answer where the
// desktop's appended a word to its dropdown. The text and the caret are the editor's as they are now (the request's).
// ----------------------------------------------------------------------------

namespace {

// One word of the dropdown — the desktop's m_ac.Append.
void AppendName(ibDataNode& response, ibContentType kind, const wxString& name, const wxString& help)
{
	ibDataNode& row = response.AddChild(0, 0);
	row.SetValue(ibProtocol::ibView::ibModule::Kind, static_cast<std::int32_t>(kind));
	row.SetValue(ibProtocol::ibView::ibModule::Name, name);
	row.SetValue(ibProtocol::ibView::ibModule::Help, help);
}

void AddKeywordFromObject(ibDataNode& response, const ibValue& vObject)
{
	if (vObject.GetType() != ibValueTypes::TYPE_EMPTY &&
		vObject.GetType() != ibValueTypes::TYPE_OLE) {
		for (long i = 0; i < vObject.GetNMethods(); i++) {
			AppendName(response,
				vObject.HasRetVal(i) ? ibContentType::eFunction : ibContentType::eProcedure,
				vObject.GetMethodName(i),
				vObject.GetMethodHelper(i)
			);
		}
		for (long i = 0; i < vObject.GetNProps(); i++) {
			// Scope-local props (ThisObject / ThisForm) must not
			// surface in autocomplete after a chain walk reaches a
			// foreign object (`Catalogs.Catalog1.CreateElement().` …).
			if (vObject.IsPropScoped(i)) continue;
			AppendName(response, ibContentType::eVariable, vObject.GetPropName(i), wxEmptyString);
		}
		// ⭐ THE VALUE'S OWN SURFACE IS THE WHOLE ANSWER. A module's exports are already the tail of its
		// member table (ExportThunk, bound in the descriptor's ctor) and a manager adds its manager
		// module's in FillMembers — both read the TEXT in the designer (ibExportNamesFromText). Two
		// blocks here parsed the same text again from the time the table said nothing in the designer,
		// so every exported function after a manager's or a module's dot was offered TWICE, the second
		// time without its call form (`Documents.GoodsSale.` → InvoiceSheet ×2, 2026-09-15).
	}
}

// ⭐⭐ WHAT MAY BE WRITTEN HERE — the keywords, which are the language's own, and then every name in
// scope, which is the COMPILER's answer (ibNamesAtCaret answers the keywords WITH the names).
void LoadSysKeyword(ibDataNode& response, const wxString& text, unsigned int caret,
	const ibValueMetaObjectModuleBase* moduleObject)
{
	// The mode covers the whole answer, not just the value door: building the list compiles the
	// text, and a name half-written is ordinary here rather than an error (backend_core.h).
	const ibBackendException::ibEvalModeScope answering(eval_complete);

	std::vector<ibCaretName> names;
	if (!ibNamesAtCaret(text, caret, moduleObject, names))
		return;

	// The declaration order and the ladder's visibility are decided on the other side — see
	// ibNamesAtCaret. What is left here is which ICON each name wears, which is presentation.
	for (const ibCaretName& name : names) {

		const ibContentType kind = !name.m_callable
			? (name.m_exported ? ibContentType::eExportVariable : ibContentType::eVariable)
			: name.m_returnsValue
				? (name.m_exported ? ibContentType::eExportFunction : ibContentType::eFunction)
				: (name.m_exported ? ibContentType::eExportProcedure : ibContentType::eProcedure);

		AppendName(response, kind, name.m_name, name.m_signature);
	}
}

// ⭐⭐ THE MEMBERS OF WHAT THE CARET STANDS AFTER. The value is COMPUTED — by the compiler, over the
// instructions it emitted for this very text — and this side only asks the value what it offers.
void LoadIntelliList(ibDataNode& response, const wxString& text, unsigned int caret,
	const ibValueMetaObjectModuleBase* moduleObject)
{
	// Constructors run and methods on values already in hand are called, and every one of those
	// asks the session what kind of evaluation it is inside (backend_core.h, eval_complete).
	const ibBackendException::ibEvalModeScope answering(eval_complete);

	// ⭐ EVERY BRANCH GOES IN — a composite field is one name and several things to walk into, and a
	// dropdown is a merged list by nature (scriptComplete.h).
	std::vector<ibCaretValue> values;
	if (ibValueAtCaret(text, caret, moduleObject, values)) {
		for (const ibCaretValue& value : values)
			AddKeywordFromObject(response, value.m_value);
	}
}

// ⭐ WHICH CALLS HAVE NAMES TO OFFER — and the answer lives HERE, with the metadata that holds
// them, not in the lexer. A `false` back means this one has nothing, and the caret is then in open
// code like any other.
bool LoadFromKeyWord(ibDataNode& response, const wxString& keyword, const ibValueMetaObjectModuleBase* moduleObject)
{
	if (stringUtils::CompareString(keyword, wxT("new"))) {
		for (auto class_obj : ibValue::GetListCtorsByType(ibCtorObjectType::ibCtorObjectType_object_value))
			AppendName(response, ibContentType::eVariable, class_obj->GetClassName(), wxEmptyString);
	}
	else if (stringUtils::CompareString(keyword, wxT("type")))
	{
		for (auto class_obj : ibValue::GetListCtorsByType(ibCtorObjectType::ibCtorObjectType_object_primitive))
			AppendName(response, ibContentType::eVariable, class_obj->GetClassName(), wxEmptyString);

		for (auto class_obj : ibValue::GetListCtorsByType(ibCtorObjectType::ibCtorObjectType_object_value))
			AppendName(response, ibContentType::eVariable, class_obj->GetClassName(), wxEmptyString);

		for (auto class_obj : ibValue::GetListCtorsByType(ibCtorObjectType::ibCtorObjectType_object_control))
			AppendName(response, ibContentType::eVariable, class_obj->GetClassName(), wxEmptyString);

		for (auto class_obj : ibValue::GetListCtorsByType(ibCtorObjectType::ibCtorObjectType_object_system))
			AppendName(response, ibContentType::eVariable, class_obj->GetClassName(), wxEmptyString);

		for (auto class_obj : ibValue::GetListCtorsByType(ibCtorObjectType::ibCtorObjectType_object_enum))
			AppendName(response, ibContentType::eVariable, class_obj->GetClassName(), wxEmptyString);

		const ibMetaData* metaData = moduleObject->GetMetaData();
		wxASSERT(metaData);

		for (auto class_obj : metaData->GetListCtorsByType(ibCtorObjectMetaType::ibCtorObjectMetaType_Object))
			AppendName(response, ibContentType::eVariable, class_obj->GetClassName(), wxEmptyString);
		for (auto class_obj : metaData->GetListCtorsByType(ibCtorObjectMetaType::ibCtorObjectMetaType_Reference))
			AppendName(response, ibContentType::eVariable, class_obj->GetClassName(), wxEmptyString);
		for (auto class_obj : metaData->GetListCtorsByType(ibCtorObjectMetaType::ibCtorObjectMetaType_Manager))
			AppendName(response, ibContentType::eVariable, class_obj->GetClassName(), wxEmptyString);
		for (auto class_obj : metaData->GetListCtorsByType(ibCtorObjectMetaType::ibCtorObjectMetaType_Selection))
			AppendName(response, ibContentType::eVariable, class_obj->GetClassName(), wxEmptyString);
	}
	else if (stringUtils::CompareString(keyword, wxT("showCommonForm"))
		|| stringUtils::CompareString(keyword, wxT("getCommonForm")))
	{
		const ibMetaData* metaData = moduleObject->GetMetaData();
		wxASSERT(metaData);

		for (const auto object : metaData->GetAnyArrayObject(g_metaCommonFormCLSID))
			AppendName(response, ibContentType::eVariable, object->GetName(), wxEmptyString);
	}
	else {
		return false;
	}

	return true;
}

// The desktop's LoadAutoComplete, its question by the place the caret stands in — read by a stream of the text in the
// session's syntax, as the desktop's editor kept one (m_tc).
void LoadAutoComplete(ibDataNode& response, const wxString& text, unsigned int caret,
	const ibValueMetaObjectModuleBase* moduleObject)
{
	ibTranslateCode stream(ibCompileCode::GetCodeStyle());
	stream.SetLexemMode(ibLexemMode::Editing);
	stream.Load(text);
	try {
		stream.Translate();
	}
	catch (...) {
	}

	const ibTranslateCode::ibCaretText at = stream.CaretAt(caret);
	switch (at.m_place) {

	case ibTranslateCode::ibCaretPlace::AfterDot:
		LoadIntelliList(response, text, caret, moduleObject);
		break;

	// ⭐ A CALL WHOSE ARGUMENT NAMES NOTHING IS OPEN CODE. The stream reports every call the caret
	// stands inside; only some of them have names to offer, and that is this side's knowledge. When
	// it has none, the question was never about the call — fall through to what is in scope.
	case ibTranslateCode::ibCaretPlace::InKeyword:
		if (!LoadFromKeyWord(response, at.m_keyword, moduleObject))
			LoadSysKeyword(response, text, caret, moduleObject);
		break;

	default:
		LoadSysKeyword(response, text, caret, moduleObject);
		break;
	}
}

// ⭐ THE CALL FORM OF WHAT IS BEING WRITTEN — the same two doors as the dropdown, asked for one
// name instead of all of them. A member's help comes from the value that holds it; a written
// function's from its declared parameters, which ibNamesAtCaret already builds as `m_signature`.
wxString LoadCallTip(const wxString& text, unsigned int caret, const ibValueMetaObjectModuleBase* moduleObject)
{
	ibTranslateCode stream(ibCompileCode::GetCodeStyle());
	stream.SetLexemMode(ibLexemMode::Editing);
	stream.Load(text);
	try {
		stream.Translate();
	}
	catch (...) {
	}

	const ibTranslateCode::ibCaretText at = stream.CaretAt(caret);
	if (at.m_word.IsEmpty())
		return wxEmptyString;

	const ibBackendException::ibEvalModeScope answering(eval_complete);

	wxString description;

	switch (at.m_place) {

	// `New <Class>` — the constructor's own help, which only the class can give.
	case ibTranslateCode::ibCaretPlace::InKeyword: {

		if (!stringUtils::CompareString(at.m_keyword, wxT("new")) || !ibValue::IsRegisterCtor(at.m_expression))
			break;

		const ibCtorAbstractType* ctor = ibValue::GetAvailableCtor(at.m_expression);
		const ibValue newObject = ctor->CreateObject();
		if (ibValue::ibMemberTable* members = newObject.GetPMethods()) {
			for (long idx = 0; idx < members->GetNConstructors(); idx++)
				description = members->GetConstructorHelper(idx);
		}
		break;
	}

	case ibTranslateCode::ibCaretPlace::AfterDot: {

		std::vector<ibCaretValue> holders;
		if (!ibValueAtCaret(text, caret, moduleObject, holders))
			break;

		for (ibCaretValue& holder : holders)
		for (long i = 0; i < holder.m_value.GetNMethods(); i++) {
			if (holder.m_value.GetMethodName(i).IsSameAs(at.m_word, false)) {
				description = holder.m_value.GetMethodHelper(i);
				break;
			}
		}
		break;
	}

	default: {

		std::vector<ibCaretName> names;
		if (!ibNamesAtCaret(text, caret, moduleObject, names))
			break;

		for (const ibCaretName& name : names) {
			if (name.m_callable && stringUtils::CompareString(name.m_name, at.m_word)) {
				description = name.m_signature;
				break;
			}
		}
		break;
	}
	}

	return description;
}


// ----------------------------------------------------------------------------
// THE LINQ CONSTRUCTOR'S QUESTIONS (ibProtocol::ibView::ibModule::ibLinq) — the desktop's window asked them of the
// compiler itself (frontend/win/dlgs/linqConstructor/linqConstructor.cpp); here they are its code, run where the
// compiler is, and answered in one exchange per action of the person's.
// ----------------------------------------------------------------------------

namespace linq = ibProtocol::ibView::ibModule::ibLinq;

// The text a probe is appended to (the desktop's m_probeHead): the module up to the START of the anchor's line — the
// statements above are finished, the one the block stands in may not be (`q = |`), and a probe glued onto half a
// statement asks the walk about a sentence nobody wrote. Built here, so a probe crosses the wire without it.
wxString LinqProbeHead(const wxString& text, unsigned int anchor)
{
	const int lineStart = text.Left(anchor).Find(wxT('\n'), /*fromEnd*/ true);
	return lineStart == wxNOT_FOUND ? wxString() : text.Left(lineStart + 1);
}

// The desktop's ibDialogLinqConstructor::Ask — AS A COMPLETION, like the dropdown: answering compiles the text and walks
// it, values are made on the way, and a name not written yet is ordinary here rather than an error. One Values row,
// named by the probe as asked, without the head.
std::vector<wxString> WriteLinqProbe(ibDataNode& values, const wxString& probeHead, const wxString& probe,
	const ibValueMetaObjectModuleBase* moduleObject)
{
	bool resolved = false;
	wxString type;
	std::vector<wxString> names;
	{
		const ibBackendException::ibEvalModeScope answering(eval_complete);
		const wxString asked = probeHead + probe;
		std::vector<ibCaretValue> found;
		try {
			if (ibValueAtCaret(asked, (unsigned int)asked.length(), moduleObject, found)) {
				resolved = !found.empty();
				for (ibCaretValue& branch : found) {
					ibValue& value = branch.m_value;
					if (type.IsEmpty()) {
						try {
							type = value.GetClassName();
						}
						catch (...) {
						}
					}
					for (long i = 0; i < value.GetNProps(); i++) {
						// The same filter the editor applies: a scope-local name belongs to its frame.
						if (value.IsPropScoped(i))
							continue;
						const wxString name = value.GetPropName(i);
						if (std::find(names.begin(), names.end(), name) == names.end())
							names.push_back(name);
					}
				}
			}
		}
		catch (...) {
			resolved = false;
			type.clear();
			names.clear();
		}
	}

	ibDataNode& row = values.AddChild(0, 0);
	row.SetValue(linq::Text, probe);
	row.SetValue(linq::Resolved, resolved);
	if (!type.IsEmpty())
		row.SetValue(linq::Type, type);
	ibDataNode& written = row.Child(linq::Names);
	for (const wxString& name : names)
		written.AddChild(0, 0).SetValue(linq::Name, name);
	return names;
}

// The probe the desktop's MembersOf builds — what follows `<expression>.` at the anchor's line.
wxString LinqMembersProbe(const wxString& expression)
{
	return wxT("linqProbe = ") + expression + wxT(".");
}

// The desktop's KindIcon rule: `Data.Catalogs` holds catalogs, and a catalog is what the type registry calls "Catalog"
// (`ChartsOf...` into `ChartOf...`, a plural into its singular) — the picture of that class, by its id; none: empty.
wxString LinqKindPicture(const wxString& section, const ibMetaData* metaData)
{
	wxString kind = section;
	if (kind.StartsWith(wxT("ChartsOf")))
		kind = wxT("ChartOf") + kind.Mid(8);
	else if (kind.EndsWith(wxT("s")))
		kind.RemoveLast();
	return ibKindPicture(kind, metaData);
}

// Open — the desktop's constructor before its window is drawn: the query the caret stands in, where the block will
// stand, what is visible there, and what each of those and `Data` hold. Returns where the block will stand.
unsigned int WriteLinqOpen(ibDataNode& response, const wxString& text, unsigned int caret,
	const ibValueMetaObjectModuleBase* moduleObject)
{
	const ibMetaData* const metaData = moduleObject->GetMetaData();
	caret = std::min<unsigned int>(caret, (unsigned int)text.length());
	unsigned int anchor = caret;
	{
		const ibBackendException::ibEvalModeScope answering(eval_complete);
		std::vector<ibQueryOutline> outlines;
		try {
			outlines = ibOutlineScriptQueries(text, moduleObject->GetName(), metaData);
		}
		catch (...) {
		}
		const ibQueryOutline* here = nullptr;
		for (const ibQueryOutline& outline : outlines) {
			if (outline.m_isRestrict || outline.m_textTo <= outline.m_textFrom)
				continue;
			if (caret < outline.m_textFrom || caret > outline.m_textTo)
				continue;
			if (here == nullptr || outline.m_textFrom > here->m_textFrom)
				here = &outline;   // the innermost, when one query stands inside another
		}
		if (here != nullptr) {
			// Where the query ends is the lexer's to say (the window reads it from here as the desktop's did); where it
			// starts, without the whitespace before it, is where the names are asked.
			const wxString rest = text.Mid(here->m_textFrom);
			ibLinqBlock opened(ibCompileCode::GetCodeStyle());
			wxString refusal;
			size_t consumed = 0;
			response.Child(linq::Query).SetValue(linq::From, static_cast<std::int32_t>(here->m_textFrom));
			if (ibLinqBlock::Parse(rest, opened, refusal, &consumed)) {
				const wxString read = rest.Left(consumed);
				const wxString trimmedLeft = wxString(read).Trim(false);
				anchor = here->m_textFrom + (unsigned int)(read.length() - trimmedLeft.length());
			}
		}
	}
	response.SetValue(linq::Anchor, static_cast<std::int32_t>(anchor));

	const wxString probeHead = LinqProbeHead(text, anchor);

	// WHAT IS VISIBLE WHERE THE BLOCK STANDS — the module's variables, the procedure's locals and parameters, the
	// object's own attributes and tabular sections.
	std::vector<wxString> variables;
	{
		const ibBackendException::ibEvalModeScope answering(eval_complete);
		std::vector<ibCaretName> names;
		try {
			ibNamesAtCaret(text, anchor, moduleObject, names);
		}
		catch (...) {
		}
		for (const ibCaretName& name : names) {
			if (name.m_callable)
				continue;
			if (name.m_origin != ibNameOrigin::Declared && name.m_origin != ibNameOrigin::Member)
				continue;
			if (std::find(variables.begin(), variables.end(), name.m_name) == variables.end())
				variables.push_back(name.m_name);
		}
	}
	ibDataNode& names = response.Child(linq::Names);
	for (const wxString& name : variables)
		names.AddChild(0, 0).SetValue(linq::Name, name);

	// …and what each holds, and `Data`'s sections — asked once for every tree, as the desktop's did.
	ibDataNode& values = response.Child(linq::Values);
	for (const wxString& name : variables)
		WriteLinqProbe(values, probeHead, LinqMembersProbe(name), moduleObject);
	const std::vector<wxString> sections = WriteLinqProbe(values, probeHead, LinqMembersProbe(wxT("Data")), moduleObject);

	ibDataNode& kinds = response.Child(linq::Kinds);
	for (const wxString& section : sections) {
		ibDataNode& kind = kinds.AddChild(0, 0);
		kind.SetValue(linq::Name, section);
		const wxString picture = LinqKindPicture(section, metaData);
		if (!picture.IsEmpty())
			kind.SetValue(linq::Picture, picture);
	}
	return anchor;
}

// Probes — each asked at its end, after `probeHead`; a Deep one's names asked too, its text with `<name>.` after it
// (the desktop's MembersOf of `<expression>.<name>`, which AddPlace asked one by one).
void WriteLinqProbes(ibDataNode& response, const ibDataNode& request, const wxString& probeHead,
	const ibValueMetaObjectModuleBase* moduleObject)
{
	const auto texts = [&request](const char* key) {
		std::vector<wxString> out;
		if (const ibDataValue* const listed = request.FindField(key)) {
			if (listed->Kind() == ibDataKind::Array)
				for (const ibDataValue& one : listed->AsArray())
					out.push_back(one.AsString());
		}
		return out;
	};
	const std::vector<wxString> probes = texts(linq::Probes);
	const std::vector<wxString> deep = texts(linq::Deep);
	if (probes.empty())
		return;

	ibDataNode& values = response.Child(linq::Values);
	for (const wxString& probe : probes) {
		const std::vector<wxString> names = WriteLinqProbe(values, probeHead, probe, moduleObject);
		if (std::find(deep.begin(), deep.end(), probe) == deep.end())
			continue;
		for (const wxString& name : names)
			WriteLinqProbe(values, probeHead, probe + name + wxT("."), moduleObject);
	}
}

// Verdict — the desktop's ShowResult: the compiler reads the block where it will stand and says what it understood.
void WriteLinqVerdict(ibDataNode& response, const wxString& text, unsigned int anchor,
	const ibValueMetaObjectModuleBase* moduleObject)
{
	std::vector<ibQueryOutline> outlines;
	{
		const ibBackendException::ibEvalModeScope answering(eval_complete);
		try {
			outlines = ibOutlineScriptQueries(text, moduleObject->GetName(), moduleObject->GetMetaData());
		}
		catch (...) {
		}
	}

	// The query that starts where the block does: the compiler notes a query from where its `From` was looked for,
	// which may be a little before the word itself.
	const ibQueryOutline* read = nullptr;
	for (const ibQueryOutline& outline : outlines) {
		if (outline.m_isRestrict || outline.m_textFrom > anchor + 1)
			continue;
		if (outline.m_textTo != 0 && outline.m_textTo <= anchor)
			continue;
		if (read == nullptr || outline.m_textFrom > read->m_textFrom)
			read = &outline;
	}
	if (read == nullptr)
		return;

	ibDataNode& written = response.Child(linq::Read);
	{
		ibDataNode& bindings = written.Child(linq::Bindings);
		for (const ibQueryOutlineBinding& binding : read->m_bindings) {
			ibDataNode& row = bindings.AddChild(0, 0);
			row.SetValue(linq::Name, binding.m_name);
			row.SetValue(linq::Origin, binding.m_origin);
			row.SetValue(linq::Offers, static_cast<std::int32_t>(binding.m_offers.size()));
		}
	}
	ibDataNode& columns = written.Child(linq::Columns);
	for (const wxString& column : read->m_columns)
		columns.AddChild(0, 0).SetValue(linq::Name, column);
}

} // namespace

bool ibModuleTextView::Fetch(const ibDataNode& request, ibDataNode& response)
{
	const ibValueMetaObjectModuleBase* const creator =
		GetDocument()->ConvertMetaObjectToType<ibValueMetaObjectModuleBase>();
	if (creator == nullptr)
		return false;

	// THE QUERY CONSTRUCTOR opened over this module's query asks the module's configuration — its sources, a source's
	// fields and parameters, and a reference's fields one level down (the composer's Expand).
	if (ibFetchQueryConstructorRequest(request, response, creator->GetMetaData()))
		return true;
	if (static_cast<ibProtocol::ibView::ibComposer::ibRequest>(request.GetValue<std::int32_t>(ibProtocol::ibView::ibComposer::Kind))
	    == ibProtocol::ibView::ibComposer::ibRequest::Expand)
		return ibFetchComposerRequest(request, response, creator->GetMetaData());

	const wxString text = request.GetValue<wxString>(ibProtocol::ibView::ibModule::Text);
	const unsigned int caret = static_cast<unsigned int>(request.GetValue<std::int32_t>(ibProtocol::ibView::ibModule::Position));

	switch (static_cast<ibProtocol::ibView::ibModule::ibRequest>(request.GetValue<std::int32_t>(ibProtocol::ibView::ibModule::Kind))) {
	case ibProtocol::ibView::ibModule::ibRequest::Complete:
		LoadAutoComplete(response, text, caret, creator);
		return true;
	case ibProtocol::ibView::ibModule::ibRequest::CallTip:
		response.SetValue(ibProtocol::ibView::ibModule::Text, LoadCallTip(text, caret, creator));
		return true;
	case ibProtocol::ibView::ibModule::ibRequest::Linq: {
		// Each part the request names, in one answer (ibLinq). The probes are asked where the block stands: the
		// opening's answer, or Position.
		unsigned int anchor = caret;
		if (request.GetValue<bool>(linq::Open))
			anchor = WriteLinqOpen(response, text, caret, creator);
		WriteLinqProbes(response, request, LinqProbeHead(text, anchor), creator);
		if (request.GetValue<bool>(linq::Verdict))
			WriteLinqVerdict(response, text, caret, creator);
		return true;
	}
	case ibProtocol::ibView::ibModule::ibRequest::Procedures:
		// The handlers the module may have (the desktop's procedures dialog offered those not written yet) — their
		// parameters as a template writes them.
		for (unsigned int idx = 0; idx < creator->GetDefaultProcedureCount(); idx++) {
			wxString procArgs;
			for (const wxString& arg : creator->GetDefaultProcedureArgs(idx)) {
				if (!procArgs.IsEmpty())
					procArgs += wxT(", ");
				procArgs += arg;
			}
			ibDataNode& row = response.AddChild(0, 0);
			row.SetValue(ibProtocol::ibView::ibModule::Name, creator->GetDefaultProcedureName(idx));
			row.SetValue(ibProtocol::ibView::ibModule::Args, procArgs);
		}
		return true;
	default:
		// Only the text — whether it takes typing is the view's State, drawn with the frame.
		response.SetValue(ibProtocol::ibView::ibModule::Text, creator->GetModuleText());
		return true;
	}
}

bool ibModuleTextView::OnClientEvent(ibProtocol::ibEvent::ibKind event, const ibDataNode& args)
{
	ibMetaDocument* const document = GetDocument();

	// The check reads the module, and is asked of one that may not be edited too (the desktop's toolbar had it there).
	if (event == ibProtocol::ibEvent::ibKind::Command) {
		const ibModuleTextDocument* const moduleDocument = dynamic_cast<const ibModuleTextDocument*>(document);
		if (moduleDocument == nullptr
			|| static_cast<ibProtocol::ibView::ibModule::ibCommand>(args.GetValue<std::int32_t>(ibProtocol::ibView::ibModule::Id)) != ibProtocol::ibView::ibModule::ibCommand::SyntaxControl)
			return false;
		moduleDocument->SyntaxControl(true);
		return true;
	}

	ibValueMetaObjectModuleBase* const creator =
		document->ConvertMetaObjectToType<ibValueMetaObjectModuleBase>();
	if (creator == nullptr || !creator->IsEditable())
		return false;

	switch (event) {
	case ibProtocol::ibEvent::ibKind::Input:
		document->Modify(true);
		return true;
	case ibProtocol::ibEvent::ibKind::Change:
		// Where the desktop's editor wrote on save (ibCodeEditorDesigner::SaveModule) — the metaobject's text — and the
		// configuration modified (ibMetaDocument::Modify).
		creator->SetModuleText(args.GetValue<wxString>(ibProtocol::ibView::ibModule::Text));
		document->Modify(true);
		return true;
	default:
		return false;
	}
}

// ----------------------------------------------------------------------------
// ibModuleTextDocument implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibModuleTextDocument, ibMetaDocument);

bool ibModuleTextDocument::OnSaveModified()
{
	// THE GATE OF A CLOSE (ibDocument::CanClose), asked by the tab's door in the close's work (ibClientChildFrame::Close).
	// The desktop checked the module in OnCloseDocument, which here runs in the frame's drawing: a question there waits
	// for an answer the drawing never sends — the client's close hung (2026-10-14). The drawing asks the gate again
	// (ibDocument::Close) and is let through: the close was answered. The client's frame closing its documents is let
	// through as well — nobody is there to answer.
	const ibClientFrame* const frame = ibClientFrame::GetFrame();
	if (frame == nullptr || frame->IsClosingWindow() || m_closeAllowed)
		return true;

	const ibValueMetaObject* const metaObject = GetMetaObject();
	if (metaObject != nullptr && metaObject->IsEditable()) {
		if (!SyntaxControl(false))
			return false;
	}
	m_closeAllowed = true;
	return ibMetaDocument::OnSaveModified();
}

bool ibModuleTextDocument::Save()
{
	const ibValueMetaObject* const metaObject = GetMetaObject();
	if (metaObject != nullptr && metaObject->IsEditable()) {
		if (!SyntaxControl(false))
			return false;
	}
	return ibMetaDocument::Save();
}

bool ibModuleTextDocument::SyntaxControl(bool throwMessage) const
{
	const ibValueMetaObject* metaObject = GetMetaObject();
	wxASSERT(metaObject);
	const ibMetaData* metaData = metaObject->GetMetaData();
	wxASSERT(metaData);
	ibRuntimeModuleDataObject* dataRef = nullptr;
	auto* cc = metaData->GetCompileCache();
	if (cc && cc->FindCompileModule(metaObject, dataRef)) {

		// A descriptor that answers the lookup carries a module (the desktop's editor says why it is asserted).
		ibCompileModule* compileModule = dataRef->GetCompileModule();
		wxASSERT(compileModule);

		try {
			if (compileModule->Compile()) {
				if (throwMessage)
					ibValueSystemFunction::Message(_("No syntax errors detected!"));
				return true;
			}
			wxASSERT("ibCompileCode::Compile return false");
			return false;

		}
		catch (...) {

			// Asked through the session's frame, as the desktop asked in a message box; no frame — no one to ask: on.
			ibBackendDocFrame* const frame = ibSession::CurrentFrame();
			if (!throwMessage && frame != nullptr) {
				const int answer = frame->ShowModalMessage(
					_("Errors were found while checking module. Do you want to continue ?"), compileModule->GetModuleName(),
					wxYES_NO | wxCENTRE);

				if (answer == wxNO)
					return false;
			}
		}
	}

	return true;
}
