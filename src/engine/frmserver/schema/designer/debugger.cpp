#include "debugger.h"

#include "backend/debugger/debugClient.h"
#include "backend/metaCollection/metaModuleObject.h"
#include "backend/metadataConfiguration.h"
#include "backend/session/session.h"
#include "core/serialize/dataBuilder.h"

#include <algorithm>

namespace {

// A module's breakpoints, as the store keeps them — a row each: Line, Condition.
void WriteBreakpoints(const wxString& module, ibDataNode& node)
{
	ibDataNode& breakpoints = node.Child(wxT("Breakpoints"));
	for (const auto& breakpoint : debugClient->GetDebugList(module)) {
		ibDataNode& row = breakpoints.AddChild(0, 0);
		row.SetValue(wxT("Line"), static_cast<s32>(breakpoint.first));
		row.SetValue(wxT("Condition"), breakpoint.second);
	}
}

}

bool ibSchemaDebugger::AccessRight(const ibSession& session) const
{
	const ibMetaDataConfigurationBase* const metaData = session.GetMetaData();
	return metaData != nullptr && metaData->AccessRight_Administration();
}

void ibSchemaDebugger::Build(const ibSession& session, const ibDataNode& args, ibDataNode& result) const
{
	if (debugClient == nullptr)
		return;

	const wxString module = args.GetValue<wxString>(wxT("Module"));
	if (!module.IsEmpty()) {
		WriteBreakpoints(module, result);
		return;
	}

	const ibMetaDataConfigurationBase* const metaData = session.GetMetaData();
	if (metaData == nullptr)
		return;
	result.SetValue(wxT("Guid"), metaData->GetConfigGuid().str());

	// Each module with its lines, as the desktop's tree told its debugger at load (OnBeforeRunMetaObject) — and its
	// breakpoints, so the client's load is one exchange, not one more for each module.
	ibDataNode& modules = result.Child(wxT("Rows"));
	for (const ibValueMetaObjectModuleBase* moduleObject : metaData->GetAnyArrayObject<ibValueMetaObjectModuleBase>(true)) {
		const wxString& text = moduleObject->GetModuleText();
		ibDataNode& row = modules.AddChild(0, 0);
		row.SetValue(wxT("Module"), moduleObject->GetDocPath());
		row.SetValue(wxT("Count"), static_cast<s32>(1 + std::count(text.begin(), text.end(), wxT('\n'))));
		WriteBreakpoints(moduleObject->GetDocPath(), row);
	}
}

std::function<void()> ibSchemaDebugger::Command(const ibSession& WXUNUSED(session), s32 command,
	const ibDataNode& args, ibProtocolRefusal& refusal, wxString& error) const
{
	if (debugClient == nullptr) {
		refusal = ibProtocolRefusal::NotFound;
		error = wxT("the configuration has no debugger");
		return nullptr;
	}

	// The lines are the committed ones — the client resolved its editor's (ResolveOriginalLine); here nothing is edited,
	// so the session's client takes them as they are and keeps its rows the same as the base's.
	const wxString module = args.GetValue<wxString>(wxT("Module"));
	const unsigned int line = static_cast<unsigned int>(args.GetValue<s32>(wxT("Line")));

	wxString said;
	switch (static_cast<ibProtocolDebuggerCommand>(command)) {
	case ibProtocolDebuggerCommand::ToggleBreakpoint:
		if (debugClient->ToggleBreakpoint(module, line, &said, args.GetValue<wxString>(wxT("Condition"))))
			return []() {};
		break;
	case ibProtocolDebuggerCommand::RemoveBreakpoint:
		if (debugClient->RemoveBreakpoint(module, line))
			return []() {};
		said = wxString::Format(wxT("the module %s has no line %u"), module, line);
		break;
	case ibProtocolDebuggerCommand::RemoveAllBreakpoint:
		if (debugClient->RemoveAllBreakpoint(&said))
			return []() {};
		break;
	default:
		refusal = ibProtocolRefusal::NotFound;
		error = wxString::Format(wxT("the debugger has no command %d"), command);
		return nullptr;
	}

	refusal = ibProtocolRefusal::BadParameter;
	error = said;
	return nullptr;
}
