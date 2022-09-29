////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuider 
//	Description : command processor
////////////////////////////////////////////////////////////////////////////

#include "cmdProc.h"

CCommandProcessor::CCommandProcessor() : wxCommandProcessor(), m_savePoint(0) {}

CCommandProcessor::~CCommandProcessor()
{
	while (!m_redoStack.empty())
	{
		CCommand *redoCmd = m_redoStack.top(); delete redoCmd;
		m_redoStack.pop();
	}

	while (!m_undoStack.empty())
	{
		CCommand *undoCmd = m_undoStack.top(); delete undoCmd;
		m_undoStack.pop();
	}
}

#include "metadata/metadata.h"
#include "frontend/visualView/visualEditor.h"
#include "metadata/metaObjects/metaFormObject.h"

void CCommandProcessor::Execute(CCommand *command)
{
	command->Execute();
	m_undoStack.push(command);

	while (!m_redoStack.empty()) {
		m_redoStack.pop();
	}

	if (g_visualHostContext) {
		g_visualHostContext->m_document->Modify(true);
	}
}

bool CCommandProcessor::Undo()
{
	if (!m_undoStack.empty())
	{
		CCommand *command = m_undoStack.top();
		m_undoStack.pop();

		command->Restore();
		m_redoStack.push(command);
	}

	return true;
}

bool CCommandProcessor::Redo()
{
	if (!m_redoStack.empty())
	{
		CCommand *command = m_redoStack.top();
		m_redoStack.pop();

		command->Execute();
		m_undoStack.push(command);
	}

	return true;
}

void CCommandProcessor::Reset()
{
	while (!m_redoStack.empty())
		m_redoStack.pop();

	while (!m_undoStack.empty())
		m_undoStack.pop();

	m_savePoint = 0;
}

bool CCommandProcessor::CanUndo() const
{
	return (!m_undoStack.empty());
}

bool CCommandProcessor::CanRedo() const
{
	return (!m_redoStack.empty());
}

void CCommandProcessor::SetSavePoint()
{
	m_savePoint = m_undoStack.size();
}

bool CCommandProcessor::IsAtSavePoint()
{
	return m_savePoint == m_undoStack.size();
}

///////////////////////////////////////////////////////////////////////////////

CCommand::CCommand() : m_executed(false) {}

void CCommand::Execute()
{
	if (!m_executed)
	{
		DoExecute();
		m_executed = true;
	}
}

void CCommand::Restore()
{
	if (m_executed)
	{
		DoRestore();
		m_executed = false;
	}
}

