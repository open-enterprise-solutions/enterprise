#ifndef __COMMAND_PROCESSOR_H__
#define __COMMAND_PROCESSOR_H__

// A DOCUMENT'S COMMANDS, AND THEIR UNDO — wx's command processor (wx/cmdproc.h) copied as it is and made ours: the
// subsystem is the document's, and has no reason to depend on the widget library. What is gone is what was the
// desktop's alone — the Edit menu it kept (SetEditMenu, SetMenuStrings) and the menu's accelerators: a client draws
// its own Undo and Redo from CanUndo / CanRedo and the labels.

#include <list>
#include <memory>

#include <wx/string.h>

#include "frmserver/frmserver.h"

// ----------------------------------------------------------------------------
// ibCommand: a single command capable of performing itself
// ----------------------------------------------------------------------------

class FRMSERVER_API ibCommand {
public:

	ibCommand(bool canUndoIt = false, const wxString& name = wxEmptyString)
		: m_canUndo(canUndoIt), m_commandName(name) {}
	virtual ~ibCommand() = default;

	// Override this to perform a command
	virtual bool Do() = 0;

	// Override this to undo a command
	virtual bool Undo() = 0;

	virtual bool CanUndo() const { return m_canUndo; }
	virtual wxString GetName() const { return m_commandName; }

protected:

	bool     m_canUndo;
	wxString m_commandName;
};

// ----------------------------------------------------------------------------
// ibCommandProcessor: ibCommand manager
// ----------------------------------------------------------------------------

class FRMSERVER_API ibCommandProcessor {
public:

	using ibCommandList = std::list<std::unique_ptr<ibCommand>>;

	// if max number of commands is -1, it is unlimited
	ibCommandProcessor(int maxCommands = -1);
	virtual ~ibCommandProcessor();

	// Pass a command to the processor. The processor calls Do(); if successful, it is appended to the command
	// history unless storeIt is false. The processor owns the command either way.
	virtual bool Submit(ibCommand* command, bool storeIt = true);

	// just store the command without executing it
	virtual void Store(ibCommand* command);

	virtual bool Undo();
	virtual bool Redo();
	virtual bool CanUndo() const;
	virtual bool CanRedo() const;

	// Initialises the current command.
	virtual void Initialize();

	// What undoing and redoing would do now — "Undo Typing", "Redo", …: the labels the desktop put on its menu.
	wxString GetUndoLabel() const;
	wxString GetRedoLabel() const;

	// command list access
	const ibCommandList& GetCommands() const { return m_commands; }
	ibCommand* GetCurrentCommand() const { return m_currentCommand != m_commands.end() ? m_currentCommand->get() : nullptr; }
	int GetMaxCommands() const { return m_maxNoCommands; }
	virtual void ClearCommands();

	// Has the current project been changed?
	virtual bool IsDirty() const;

	// Mark the current command as the one where the last save took place
	void MarkAsSaved() { m_lastSavedCommand = m_currentCommand; }

protected:

	// for further flexibility, command processor doesn't call ibCommand::Do() and Undo() directly but uses these
	// functions which can be overridden in the derived class
	virtual bool DoCommand(ibCommand& cmd);
	virtual bool UndoCommand(ibCommand& cmd);

	int           m_maxNoCommands;
	ibCommandList m_commands;
	// The current command and the one the last save took place at — end() for none.
	ibCommandList::iterator m_currentCommand, m_lastSavedCommand;

private:

	ibCommandProcessor(const ibCommandProcessor&) = delete;
	ibCommandProcessor& operator=(const ibCommandProcessor&) = delete;
};

#endif
