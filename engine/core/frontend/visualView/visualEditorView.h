#ifndef _VISUAL_EDITOR_VIEW_H_
#define _VISUAL_EDITOR_VIEW_H_

#include "visualEditor.h"
#include "common/docInfo.h"

class CVisualView : public IVisualHost
{
	CValueForm *m_valueForm;
	CDocument *m_document;

	bool m_formDemonstration;

public:

	// construction
	CVisualView(CDocument *m_doc, CValueForm *valueForm, wxWindow *parent, bool demonstration = false) :
		IVisualHost(parent, wxID_ANY, wxDefaultPosition, parent->GetSize()),
		m_document(m_doc), m_valueForm(valueForm), m_formDemonstration(demonstration)
	{
		m_formHandler = NULL;
	}

	virtual ~CVisualView();

	void CreateFrame();
	void UpdateFrame();

	virtual bool IsDemonstration() override { return m_formDemonstration; }

	virtual CValueForm *GetValueForm() override { return m_valueForm; }
	virtual void SetValueForm(CValueForm *valueForm) { m_valueForm = valueForm; }

	virtual wxWindow *GetParentBackgroundWindow() override { return this; }
	virtual wxWindow *GetBackgroundWindow() override { return this; }

	void ShowForm();
	void ActivateForm();
	void UpdateForm();
	bool CloseForm();

protected:

	friend class CValueForm;
};

#endif