#include "debugger.h"

#include "backend/debugger/debugClient.h"
#include "backend/metadataConfiguration.h"
#include "backend/session/session.h"
#include "core/serialize/dataBuilder.h"
#include "protocol/protocolDesigner.h"   // ibProtocol::ibSchema::ibDebugger

namespace {

// A module's breakpoints at the editor's lines, as the session's client has them — a row each: Line, Condition.
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
}

std::function<void()> ibSchemaDebugger::Command(const ibSession& /*session*/, std::int32_t command,
	const ibDataNode& args, ibProtocol::ibRefusal& refusal, ibString& error) const
{
	if (debugClient == nullptr) {
		refusal = ibProtocol::ibRefusal::NotFound;
		error = ibT("the configuration has no debugger");
		return nullptr;
	}

	// The lines are the editor's — the session's client finds them in its map, which the editor's edits moved where its
	// text arrives (ibModuleTextView, Change): one map, as the desktop's one process had it.
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
