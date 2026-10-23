#include "clientDebugger.h"

#include <cstdint>

#include "backend/debugger/debugClient.h"
#include "core/serialize/dataBuilder.h"

#include "clientHost.h"   // NotifyDebug — the client told what the engine said

namespace {

// A watched row's number, as the asker gave it and the runtime says it back — a pointer of the asker's tree, carried as
// a number nothing here takes apart.
ibDataValue ItemValue(const wxTreeItemId& item)
{
	return ibDataValue::UInt(static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(item.GetID())));
}

// Where a piece of news is of — the runtime's session, the file and the module (a module by its address).
void WritePlace(const ibDebugData& data, ibDataNode& event)
{
	event.SetValue(ibProtocol::ibDebug::Session, data.m_sessionGuid);
	event.SetValue(ibProtocol::ibDebug::File, data.m_fileName);
	event.SetValue(ibProtocol::ibDebug::Module, data.m_moduleName);
}

void WriteLine(const ibDebugLineData& data, ibDataNode& event)
{
	WritePlace(data, event);
	event.SetValue(ibProtocol::ibDebug::Line, static_cast<std::int32_t>(data.m_line));
}

// A watch's answer — the row asked about, who asked, and its values, each with the row it is of.
void WriteWatch(const ibWatchWindowData& data, ibDataNode& event)
{
	event.AddField(ibProtocol::ibDebug::Item, ItemValue(data.GetItem()));
	event.SetValue(ibProtocol::ibDebug::Asker, data.GetBridgeId());
	ibDataNode& rows = event.Child(ibProtocol::ibDebug::Rows);
	for (unsigned int i = 0; i < data.GetWatchCount(); i++) {
		ibDataNode& row = rows.AddChild(0, 0);
		row.AddField(ibProtocol::ibDebug::Item, ItemValue(data.GetItem(i)));
		row.SetValue(ibProtocol::ibDebug::Name, data.GetName(i));
		row.SetValue(ibProtocol::ibDebug::Value, data.GetValue(i));
		row.SetValue(ibProtocol::ibDebug::Type, data.GetType(i));
		row.SetValue(ibProtocol::ibDebug::Attributes, data.HasAttributes(i));
	}
}

} // namespace

ibClientDebugger::ibClientDebugger(const ibApplicationInstance* applicationInstance, const ibClientInstance* clientInstance)
	: m_applicationInstance(applicationInstance), m_clientInstance(clientInstance)
{
}

bool ibClientDebugger::Command(ibProtocol::ibDebug::ibCommand command, const ibDataNode& params, ibDataNode& result,
	ibProtocol::ibRefusal& refusal, wxString& error) const
{
	if (debugClient == nullptr) {
		refusal = ibProtocol::ibRefusal::NotFound;
		error = wxT("the configuration has no debugger");
		return false;
	}

	const wxString host = params.GetValue<wxString>(ibProtocol::ibDebug::Host);
	const unsigned short port = static_cast<unsigned short>(params.GetValue<std::int32_t>(ibProtocol::ibDebug::Port));
	const bool kill = params.GetValue<bool>(ibProtocol::ibDebug::Kill);
	const wxString expression = params.GetValue<wxString>(ibProtocol::ibDebug::Expression);
	const ibDataValue* const item = params.FindField(ibProtocol::ibDebug::Item);
	const unsigned long long itemId = item != nullptr ? item->AsUInt() : 0;

	switch (command) {
	case ibProtocol::ibDebug::ibCommand::Search:
		debugClient->SearchServer(true);
		return true;
	case ibProtocol::ibDebug::ibCommand::Connections:
		// Looked for again, as the desktop's Debug items window did at each refresh, and listed as they stand.
		debugClient->SearchServer();
		{
			ibDataNode& rows = result.Child(ibProtocol::ibDebug::Rows);
			debugClient->ForEachConnection([&rows](const auto& connection) {
				ibDataNode& row = rows.AddChild(0, 0);
				row.SetValue(ibProtocol::ibDebug::Host, connection.GetHostName());
				row.SetValue(ibProtocol::ibDebug::Port, static_cast<std::int32_t>(connection.GetPort()));
				row.SetValue(ibProtocol::ibDebug::Type, static_cast<std::int32_t>(connection.GetConnectionType()));
				row.SetValue(ibProtocol::ibDebug::Verified, connection.IsVerifiedConnection());
				row.SetValue(ibProtocol::ibDebug::Connected, connection.IsConnected());
				row.SetValue(ibProtocol::ibDebug::User, connection.GetUserName());
				row.SetValue(ibProtocol::ibDebug::Computer, connection.GetComputerName());
			});
		}
		return true;
	case ibProtocol::ibDebug::ibCommand::Attach:
		debugClient->AttachConnection(host, port);
		return true;
	case ibProtocol::ibDebug::ibCommand::Detach:
		debugClient->DetachConnection(host, port, kill);
		return true;
	case ibProtocol::ibDebug::ibCommand::Continue:
		debugClient->Continue();
		return true;
	case ibProtocol::ibDebug::ibCommand::StepOver:
		debugClient->StepOver();
		return true;
	case ibProtocol::ibDebug::ibCommand::StepInto:
		debugClient->StepInto();
		return true;
	case ibProtocol::ibDebug::ibCommand::StepOut:
		debugClient->StepOut();
		return true;
	case ibProtocol::ibDebug::ibCommand::Pause:
		debugClient->Pause();
		return true;
	case ibProtocol::ibDebug::ibCommand::Stop:
		debugClient->Stop(kill);
		return true;
	case ibProtocol::ibDebug::ibCommand::ToolTip:
		debugClient->EvaluateToolTip(params.GetValue<wxString>(ibProtocol::ibDebug::File),
			params.GetValue<wxString>(ibProtocol::ibDebug::Module), expression);
		return true;
	case ibProtocol::ibDebug::ibCommand::AddExpression:
		debugClient->AddExpression(expression, itemId, params.GetValue<wxString>(ibProtocol::ibDebug::Asker));
		return true;
	case ibProtocol::ibDebug::ibCommand::ExpandExpression:
		debugClient->ExpandExpression(expression, itemId, params.GetValue<wxString>(ibProtocol::ibDebug::Asker));
		return true;
	case ibProtocol::ibDebug::ibCommand::RemoveExpression:
		debugClient->RemoveExpression(itemId);
		return true;
	case ibProtocol::ibDebug::ibCommand::Stack:
		debugClient->SetLevelStack(static_cast<unsigned int>(params.GetValue<std::int32_t>(ibProtocol::ibDebug::Level)));
		return true;
	default:
		break;
	}

	refusal = ibProtocol::ibRefusal::NotFound;
	error = wxString::Format(wxT("the debugger has no command %d"), static_cast<int>(command));
	return false;
}

void ibClientDebugger::OnSessionStart()
{
	ibDataNode event;
	event.SetValue(ibProtocol::ibDebug::Connected, debugClient != nullptr && debugClient->HasConnections());
	Notify(ibProtocol::ibDebug::ibEvent::SessionStart, event);
}

void ibClientDebugger::OnSessionEnd()
{
	ibDataNode event;
	event.SetValue(ibProtocol::ibDebug::Connected, debugClient != nullptr && debugClient->HasConnections());
	Notify(ibProtocol::ibDebug::ibEvent::SessionEnd, event);
}

void ibClientDebugger::OnEnterLoop(const ibDebugLineData& data)
{
	ibDataNode event;
	WriteLine(data, event);
	Notify(ibProtocol::ibDebug::ibEvent::EnterLoop, event);
}

void ibClientDebugger::OnLeaveLoop(const ibDebugLineData& data)
{
	ibDataNode event;
	WriteLine(data, event);
	Notify(ibProtocol::ibDebug::ibEvent::LeaveLoop, event);
}

void ibClientDebugger::OnAutoComplete(const ibDebugAutoCompleteData& /*data*/)
{
}

void ibClientDebugger::OnMessageFromServer(const ibDebugLineData& data, const wxString& message)
{
	ibDataNode event;
	WriteLine(data, event);
	event.SetValue(ibProtocol::ibDebug::Text, message);
	Notify(ibProtocol::ibDebug::ibEvent::Message, event);
}

void ibClientDebugger::OnSetToolTip(const ibDebugExpressionData& data, const wxString& resultStr)
{
	ibDataNode event;
	WritePlace(data, event);
	event.SetValue(ibProtocol::ibDebug::Expression, data.m_expression);
	event.SetValue(ibProtocol::ibDebug::Text, resultStr);
	Notify(ibProtocol::ibDebug::ibEvent::ToolTip, event);
}

void ibClientDebugger::OnSetStack(const ibStackData& stackData)
{
	ibDataNode event;
	ibDataNode& rows = event.Child(ibProtocol::ibDebug::Rows);
	for (const ibStackData::ibStackRow& frame : stackData.m_stackData) {
		ibDataNode& row = rows.AddChild(0, 0);
		row.SetValue(ibProtocol::ibDebug::Module, frame.m_moduleName);
		row.SetValue(ibProtocol::ibDebug::Name, frame.m_fullName);
		row.SetValue(ibProtocol::ibDebug::Line, static_cast<std::int32_t>(frame.m_moduleLine));
	}
	Notify(ibProtocol::ibDebug::ibEvent::Stack, event);
}

void ibClientDebugger::OnSetLocalVariable(const ibLocalWindowData& watchData)
{
	ibDataNode event;
	ibDataNode& rows = event.Child(ibProtocol::ibDebug::Rows);
	for (unsigned int i = 0; i < watchData.GetVarCount(); i++) {
		ibDataNode& row = rows.AddChild(0, 0);
		row.SetValue(ibProtocol::ibDebug::Name, watchData.GetName(i));
		row.SetValue(ibProtocol::ibDebug::Value, watchData.GetValue(i));
		row.SetValue(ibProtocol::ibDebug::Type, watchData.GetType(i));
		row.SetValue(ibProtocol::ibDebug::Attributes, watchData.HasAttributes(i));
	}
	Notify(ibProtocol::ibDebug::ibEvent::Locals, event);
}

void ibClientDebugger::OnSetVariable(const ibWatchWindowData& watchData)
{
	ibDataNode event;
	WriteWatch(watchData, event);
	Notify(ibProtocol::ibDebug::ibEvent::Variable, event);
}

void ibClientDebugger::OnSetExpanded(const ibWatchWindowData& watchData)
{
	ibDataNode event;
	WriteWatch(watchData, event);
	Notify(ibProtocol::ibDebug::ibEvent::Expanded, event);
}

void ibClientDebugger::Notify(ibProtocol::ibDebug::ibEvent kind, ibDataNode& event) const
{
	event.SetValue(ibProtocol::ibDebug::Event, static_cast<std::int32_t>(kind));
	if (ibClientHost* const host = ibClientHost::Find(m_applicationInstance))
		host->NotifyDebug(m_clientInstance, event);
}
