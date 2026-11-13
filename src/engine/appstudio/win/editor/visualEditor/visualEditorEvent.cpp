////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : visual editor
////////////////////////////////////////////////////////////////////////////

#include "visualEditor.h"

/////////////////////////////////////////////////////////////////////////////
// Enterprise ibEvent Handlers
/////////////////////////////////////////////////////////////////////////////

// ⚠ The control tree only: the attribute and the command trees are drawn with the form (Draw), their rows the answer's.
void ibVisualEditorNotebook::ibVisualEditor::NotifyEditorLoaded()
{
	m_objectTree->OnEditorLoaded();
}

void ibVisualEditorNotebook::ibVisualEditor::NotifyEditorRefresh()
{
	m_objectTree->OnEditorRefresh();

	WireTableboxDrops(GetValueForm());   // (re)attach per-table drop targets after the controls rebuild
}

void ibVisualEditorNotebook::ibVisualEditor::NotifyObjectSelected(ibValueFrame* obj, bool force)
{
	m_objectTree->OnObjectSelected(obj);
}

void ibVisualEditorNotebook::ibVisualEditor::NotifyPropertyModified(ibProperty* prop)
{
	m_objectTree->OnPropertyModified(prop);
}
