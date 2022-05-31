#ifndef _FORMDESIGNERCMD_H__
#define _FORMDESIGNERCMD_H__

#include "common/cmdProc.h"
#include "frontend/visualView/visualEditor.h"

class CVisualDesignerCommandProcessor : public CCommandProcessor
{
	CFormDocument *m_document;
	CVisualEditorContextForm *m_visual;

public:

	CVisualDesignerCommandProcessor(CFormDocument *doc, CVisualEditorContextForm *visual) : CCommandProcessor(), m_document(doc), m_visual(visual) {}

	virtual bool Undo() override
	{
		if (m_document->IsEditorActivate())
		{
			m_document->GetCodeCtrl()->Undo();
		}
		else
		{
			bool resultUndo = CCommandProcessor::Undo();

			if (resultUndo)
			{
				IValueFrame *m_objControl = m_visual->GetSelectedObject();

				m_visual->NotifyProjectRefresh();
				m_visual->NotifyObjectSelected(m_objControl);
			}

			return resultUndo;
		}

		return true;
	}

	virtual bool Redo() override
	{
		if (m_document->IsEditorActivate())
		{
			m_document->GetCodeCtrl()->Redo();
		}
		else
		{
			bool resultRedo = CCommandProcessor::Redo();

			if (resultRedo)
			{
				IValueFrame *m_objControl = m_visual->GetSelectedObject();


				m_visual->NotifyProjectRefresh();
				m_visual->NotifyObjectSelected(m_objControl);
			}

			return resultRedo;
		}

		return true;
	}

	virtual bool CanUndo() const override
	{
		if (m_document->IsEditorActivate())
			return m_document->GetCodeCtrl()->CanUndo();
		else return CCommandProcessor::CanUndo();
	}

	virtual bool CanRedo() const override
	{
		if (m_document->IsEditorActivate())
			return m_document->GetCodeCtrl()->CanRedo();
		return CCommandProcessor::CanRedo();
	}
};

#endif 