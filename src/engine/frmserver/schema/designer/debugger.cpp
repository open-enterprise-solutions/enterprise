#include "debugger.h"

#include "backend/debugger/debugClient.h"
#include "backend/metaCollection/metaModuleObject.h"
#include "backend/metadataConfiguration.h"
#include "backend/session/session.h"
#include "core/serialize/dataBuilder.h"

#include <algorithm>

namespace {

// A module's breakpoints, as the store keeps them — a row each: Line, Condition.
void WriteBreakpoints(const ibString& module, ibDataNode& node)
{
	ibDataNode& breakpoints = node.Child(ibProtocol::ibSchema::ibDebugger::Breakpoints);
	for (const auto& breakpoint : debugClient->GetDebugList(module)) {
		ibDataNode& row = breakpoints.AddChild(0, 0);
		row.SetValue(ibProtocol::ibSchema::ibDebugger::Line, static_cast<std::int32_t>(breakpoint.first));
		row.SetValue(ibProtocol::ibSchema::ibDebugger::Condition, breakpoint.second);
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

	const ibString module = args.GetValue<ibString>(ibProtocol::ibSchema::ibDebugger::Module);
	if (!module.IsEmpty()) {
		WriteBreakpoints(module, result);
		return;
	}

	const ibMetaDataConfigurationBase* const metaData = session.GetMetaData();
	if (metaData == nullptr)
		return;
	result.SetValue(ibProtocol::ibSchema::ibDebugger::Guid, metaData->GetConfigGuid().str());

	// Each module with its lines, as the desktop's tree told its debugger at load (OnBeforeRunMetaObject) — and its
	// breakpoints, so the client's load is one exchange, not one more for each module.
	ibDataNode& modules = result.Child(ibProtocol::ibSchema::ibDebugger::Rows);
	for (const ibValueMetaObjectModuleBase* moduleObject : metaData->GetAnyArrayObject<ibValueMetaObjectModuleBase>(true)) {
		const ibString& text = moduleObject->GetModuleText();
		ibDataNode& row = modules.AddChild(0, 0);
		row.SetValue(ibProtocol::ibSchema::ibDebugger::Module, moduleObject->GetDocPath());
		row.SetValue(ibProtocol::ibSchema::ibDebugger::Count, static_cast<std::int32_t>(1 + std::count(text.begin(), text.end(), ibT('\n'))));
		WriteBreakpoints(moduleObject->GetDocPath(), row);
	}
}

std::function<void()> ibSchemaDebugger::Command(const ibSession& /*session*/, std::int32_t command,
	const ibDataNode& args, ibProtocol::ibRefusal& refusal, ibString& error) const
{
	if (debugClient == nullptr) {
		refusal = ibProtocol::ibRefusal::NotFound;
		error = ibT("the configuration has no debugger");
		return nullptr;
	}

	// The lines are the committed ones — the client resolved its editor's (ResolveOriginalLine); here nothing is edited,
	// so the session's client takes them as they are and keeps its rows the same as the base's.
	const ibString module = args.GetValue<ibString>(ibProtocol::ibSchema::ibDebugger::Module);
	const unsigned int line = static_cast<unsigned int>(args.GetValue<std::int32_t>(ibProtocol::ibSchema::ibDebugger::Line));

	ibString said;
	switch (static_cast<ibProtocol::ibSchema::ibDebugger::ibCommand>(command)) {
	case ibProtocol::ibSchema::ibDebugger::ibCommand::ToggleBreakpoint:
		if (debugClient->ToggleBreakpoint(module, line, &said, args.GetValue<ibString>(ibProtocol::ibSchema::ibDebugger::Condition)))
			return []() {};
		break;
	case ibProtocol::ibSchema::ibDebugger::ibCommand::RemoveBreakpoint:
		if (debugClient->RemoveBreakpoint(module, line))
			return []() {};
		said = ibString::Format(ibT("the module %s has no line %u"), module, line);
		break;
	case ibProtocol::ibSchema::ibDebugger::ibCommand::RemoveAllBreakpoint:
		if (debugClient->RemoveAllBreakpoint(&said))
			return []() {};
		break;
	default:
		refusal = ibProtocol::ibRefusal::NotFound;
		error = ibString::Format(ibT("the debugger has no command %d"), command);
		return nullptr;
	}

	refusal = ibProtocol::ibRefusal::BadParameter;
	error = said;
	return nullptr;
}
