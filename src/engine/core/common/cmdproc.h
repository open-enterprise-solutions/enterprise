#ifndef __COMMAND_PROC__
#define __COMMAND_PROC__

#include <stack>
#include <memory>

#include <wx/cmdproc.h>

class CCommand;

class CCommandProcessor : public wxCommandProcessor
{
	typedef std::stack<CCommand *> CommandStack;

	CommandStack m_undoStack;
	CommandStack m_redoStack;

	unsigned int m_savePoint;

public:

	CCommandProcessor();
	~CCommandProcessor();

	virtual void Execute(CCommand *command);

	virtual bool Undo() override;
	virtual bool Redo() override;

	virtual bool CanUndo() const override;
	virtual bool CanRedo() const override;

	void Reset();

	void SetSavePoint();
	bool IsAtSavePoint();
};

class CCommand : public wxCommand
{
	bool m_executed;

protected:

	/**
	 * Ejecuta el comando.
	 */
	virtual void DoExecute() = 0;

	/**
	 * Restaura el estado previo a la ejecución del comando.
	 */
	virtual void DoRestore() = 0;

public:

	CCommand();

	virtual void Execute();
	virtual void Restore();

	// Override this to perform a command
	virtual bool Do() override
	{
		m_canUndo = true;
		DoExecute(); return true;
	}

	// Override this to undo a command
	virtual bool Undo() override
	{
		m_canUndo = true;
		DoRestore(); return true;
	}

	virtual ~CCommand() {};
};

#endif 