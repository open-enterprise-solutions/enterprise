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
#include "core/serialize/dataBuilder.h"
#include "frmserver/client/clientFrame.h"   // IsClosingWindow — the client leaving vs a tab's cross

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
	ibDataNode* const drawn = frame.FindChild(wxT("State"));
	ibDataNode& state = drawn != nullptr ? *drawn : frame.Child(wxT("State"));
	state.SetValue(wxT("Syntax"), static_cast<s32>(ibCompileCode::GetCodeStyle()));
	if (!creator->IsEditable())
		state.SetValue(wxT("ReadOnly"), true);
	// Whether the configuration the module is in is modified — the document itself never is (ibMetaDocument::IsModified),
	// and Save is offered all along: what the desktop's editor marked on every key the client says once, on the first key
	// while this is false (event Input).
	const ibMetaData* const metaData = creator->GetMetaData();
	state.SetValue(wxT("Modified"), metaData != nullptr && metaData->IsModified());
	// The module's address the debugger knows it by — its breakpoints' key, the place a runtime stops at.
	state.SetValue(wxT("Module"), creator->GetDocPath());
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
	row.SetValue(wxT("Kind"), static_cast<s32>(kind));
	row.SetValue(wxT("Name"), name);
	row.SetValue(wxT("Help"), help);
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
		stream.PrepareLexem();
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
		stream.PrepareLexem();
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

} // namespace

bool ibModuleTextView::Fetch(const ibDataNode& request, ibDataNode& response)
{
	const ibValueMetaObjectModuleBase* const creator =
		GetDocument()->ConvertMetaObjectToType<ibValueMetaObjectModuleBase>();
	if (creator == nullptr)
		return false;

	const wxString text = request.GetValue<wxString>(wxT("Text"));
	const unsigned int caret = static_cast<unsigned int>(request.GetValue<s32>(wxT("Position")));

	switch (static_cast<ibProtocolModuleRequest>(request.GetValue<s32>(wxT("Kind")))) {
	case ibProtocolModuleRequest::Complete:
		LoadAutoComplete(response, text, caret, creator);
		return true;
	case ibProtocolModuleRequest::CallTip:
		response.SetValue(wxT("Text"), LoadCallTip(text, caret, creator));
		return true;
	case ibProtocolModuleRequest::Procedures:
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
			row.SetValue(wxT("Name"), creator->GetDefaultProcedureName(idx));
			row.SetValue(wxT("Args"), procArgs);
		}
		return true;
	default:
		// Only the text — whether it takes typing is the view's State, drawn with the frame.
		response.SetValue(wxT("Text"), creator->GetModuleText());
		return true;
	}
}

bool ibModuleTextView::OnClientEvent(ibProtocolEvent event, const ibDataNode& args)
{
	ibMetaDocument* const document = GetDocument();

	// The check reads the module, and is asked of one that may not be edited too (the desktop's toolbar had it there).
	if (event == ibProtocolEvent::Command) {
		const ibModuleTextDocument* const moduleDocument = dynamic_cast<const ibModuleTextDocument*>(document);
		if (moduleDocument == nullptr
			|| static_cast<ibProtocolModuleCommand>(args.GetValue<s32>(wxT("Id"))) != ibProtocolModuleCommand::SyntaxControl)
			return false;
		moduleDocument->SyntaxControl(true);
		return true;
	}

	ibValueMetaObjectModuleBase* const creator =
		document->ConvertMetaObjectToType<ibValueMetaObjectModuleBase>();
	if (creator == nullptr || !creator->IsEditable())
		return false;

	switch (event) {
	case ibProtocolEvent::Input:
		document->Modify(true);
		return true;
	case ibProtocolEvent::Change:
		// Where the desktop's editor wrote on save (ibCodeEditorDesigner::SaveModule) — the metaobject's text — and the
		// configuration modified (ibMetaDocument::Modify).
		creator->SetModuleText(args.GetValue<wxString>(wxT("Text")));
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
