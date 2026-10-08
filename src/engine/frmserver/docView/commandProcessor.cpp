#include "commandProcessor.h"

#include <iterator>

#include <wx/intl.h>

// ----------------------------------------------------------------------------
// ibCommandProcessor — the bodies of wx's, the list of nodes made a list of owned commands
// ----------------------------------------------------------------------------

namespace {

// A command's name, or what an unnamed one is called.
wxString CommandName(const ibCommand& command)
{
	const wxString name = command.GetName();
	return !name.empty() ? name : wxString(_("Unnamed command"));
}

} // namespace

ibCommandProcessor::ibCommandProcessor(int maxCommands)
	: m_maxNoCommands(maxCommands), m_currentCommand(m_commands.end()), m_lastSavedCommand(m_commands.end())
{
}

ibCommandProcessor::~ibCommandProcessor()
{
	ClearCommands();
}

bool ibCommandProcessor::DoCommand(ibCommand& cmd)
{
	return cmd.Do();
}

bool ibCommandProcessor::UndoCommand(ibCommand& cmd)
{
	return cmd.Undo();
}

// Pass a command to the processor. The processor calls Do(); if successful, is appended to the command history
// unless storeIt is false.
bool ibCommandProcessor::Submit(ibCommand* command, bool storeIt)
{
	wxCHECK_MSG(command, false, wxT("no command in ibCommandProcessor::Submit"));

	// the user code expects the command to be deleted anyhow
	std::unique_ptr<ibCommand> owned(command);
	if (!DoCommand(*command))
		return false;

	if (storeIt)
		Store(owned.release());
	return true;
}

void ibCommandProcessor::Store(ibCommand* command)
{
	wxCHECK_RET(command, wxT("no command in ibCommandProcessor::Store"));

	// We must chop off the current 'branch' so that we're at the end of the command list.
	if (m_currentCommand == m_commands.end())
		ClearCommands();
	else {
		for (ibCommandList::iterator node = std::next(m_currentCommand); node != m_commands.end();) {
			// Make sure m_lastSavedCommand won't point to freed memory
			if (m_lastSavedCommand == node)
				m_lastSavedCommand = m_commands.end();
			node = m_commands.erase(node);
		}
	}

	if (static_cast<int>(m_commands.size()) == m_maxNoCommands) {
		// Make sure m_lastSavedCommand won't point to freed memory
		if (m_lastSavedCommand == m_commands.begin())
			m_lastSavedCommand = m_commands.end();
		m_commands.erase(m_commands.begin());
	}

	m_commands.emplace_back(command);
	m_currentCommand = std::prev(m_commands.end());
}

bool ibCommandProcessor::Undo()
{
	ibCommand* const command = GetCurrentCommand();
	if (command && command->CanUndo()) {
		if (UndoCommand(*command)) {
			m_currentCommand = m_currentCommand != m_commands.begin() ? std::prev(m_currentCommand) : m_commands.end();
			return true;
		}
	}

	return false;
}

bool ibCommandProcessor::Redo()
{
	ibCommandList::iterator redoNode = m_commands.end();

	if (m_currentCommand != m_commands.end()) {
		// is there anything to redo?
		redoNode = std::next(m_currentCommand);
	}
	else {
		// no current command, redo the first one
		redoNode = m_commands.begin();
	}

	if (redoNode != m_commands.end()) {
		if (DoCommand(**redoNode)) {
			m_currentCommand = redoNode;
			return true;
		}
	}
	return false;
}

bool ibCommandProcessor::CanUndo() const
{
	const ibCommand* const command = GetCurrentCommand();
	return command && command->CanUndo();
}

bool ibCommandProcessor::CanRedo() const
{
	if (m_currentCommand != m_commands.end())
		return std::next(m_currentCommand) != m_commands.end();

	return !m_commands.empty();
}

void ibCommandProcessor::Initialize()
{
	m_currentCommand = !m_commands.empty() ? std::prev(m_commands.end()) : m_commands.end();
}

wxString ibCommandProcessor::GetUndoLabel() const
{
	const ibCommand* const command = GetCurrentCommand();
	if (command == nullptr)
		return _("Undo");

	return command->CanUndo()
		? wxString::Format(_("Undo %s"), CommandName(*command))
		: wxString::Format(_("Can't Undo %s"), CommandName(*command));
}

wxString ibCommandProcessor::GetRedoLabel() const
{
	// We can redo, if we're not at the end of the history — or, undone to the start of the list, the first.
	const ibCommandList::const_iterator redoNode = m_currentCommand != m_commands.end()
		? std::next(ibCommandList::const_iterator(m_currentCommand)) : m_commands.begin();
	if (redoNode == m_commands.end())
		return _("Redo");

	return wxString::Format(_("Redo %s"), CommandName(**redoNode));
}

void ibCommandProcessor::ClearCommands()
{
	m_commands.clear();
	m_currentCommand = m_commands.end();
	m_lastSavedCommand = m_commands.end();
}

bool ibCommandProcessor::IsDirty() const
{
	if (m_lastSavedCommand == m_commands.end()) {
		// We have never been saved, so we are dirty if and only if we have any commands at all.
		return m_currentCommand != m_commands.end();
	}

	if (m_currentCommand == m_commands.end()) {
		// This only happens if all commands were undone after saving the document: we're dirty then.
		return true;
	}

	// Finally if both iterators are valid, we may just compare them.
	return m_currentCommand != m_lastSavedCommand;
}
