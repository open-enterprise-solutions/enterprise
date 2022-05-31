#include "formDesigner.h"

#include "frontend/mainFrame.h"
#include "metadata/metaObjectsDefines.h" 

#include "formDesigner_cmd.h"

wxIMPLEMENT_CLASS(CFormDocument, CDocument);

CCommandProcessor *CFormDocument::CreateCommandProcessor()
{
	CVisualEditorContextForm* m_context = GetFormDesigner()->GetDesignerContext();
	CVisualDesignerCommandProcessor *commandProcessor = new CVisualDesignerCommandProcessor(this, m_context);
	m_context->SetCommandProcessor(commandProcessor);

	commandProcessor->SetEditMenu(mainFrame->GetDefaultMenu(wxID_EDIT));
	commandProcessor->Initialize();

	return commandProcessor;
}

bool CFormDocument::OnCreate(const wxString& path, long flags)
{
	if (!CDocument::OnCreate(path, flags))
		return false;

	return GetFormDesigner()->LoadForm();
}

bool CFormDocument::OnOpenDocument(const wxString& filename)
{
	return CDocument::OnOpenDocument(filename);
}

#include "frontend/visualView/visualEditor.h"

bool CFormDocument::OnSaveDocument(const wxString& filename)
{
	return GetFormDesigner()->SaveForm() &&
		CDocument::OnSaveDocument(filename);
}

#include "metadata/metadata.h"
#include "frontend/objinspect/objinspect.h"

bool CFormDocument::OnSaveModified()
{
	return CDocument::OnSaveModified();
}

bool CFormDocument::OnCloseDocument()
{
	CCodeEditorCtrl *autocomplete = GetCodeCtrl();

	if (autocomplete->IsEditable()) {

		wxASSERT(m_metaObject);
		IMetadata *metaData = m_metaObject->GetMetadata();
		wxASSERT(metaData);
		IModuleManager *moduleManager = metaData->GetModuleManager();
		wxASSERT(moduleManager);

		IModuleInfo *dataRef = NULL;

		if (moduleManager->FindCompileModule(m_metaObject, dataRef))
		{
			CCompileModule *compileModule = dataRef->GetCompileModule();

			try
			{
				if (!compileModule->Compile()) {
					wxASSERT("CCompileModule::Compile return false");
				}
			}
			catch (...)
			{
				int answer = wxMessageBox(
					_("Errors were found while checking module. Do you want to continue ?"), compileModule->GetModuleName(),
					wxYES_NO | wxCENTRE);

				if (answer == wxNO)
					return false;
			}
		}
	}

	CVisualEditorContextForm *visualContext = GetFormDesigner()->GetDesignerContext();
	wxASSERT(visualContext);
	CValueForm *valueForm = visualContext->GetValueForm();
	
	if (valueForm)
	{
		if (!valueForm->CloseForm()) {
			return false;
		}

		objectInspector->SelectObject(valueForm->GetFormMetaObject());

		// FIX!!! when demoform is opened - error 
		m_visualHostContext = NULL;
	}
	else
	{
		objectInspector->ClearProperty(); 
	}

	return CDocument::OnCloseDocument();
}

bool CFormDocument::IsModified() const
{
	return CDocument::IsModified();
}

void CFormDocument::Modify(bool modified)
{
	CDocument::Modify(modified);
}

bool CFormDocument::Save()
{
	CCodeEditorCtrl *autocomplete = GetCodeCtrl();

	if (autocomplete &&
		autocomplete->IsEditable()) {
		wxASSERT(m_metaObject);
		IMetadata *metaData = m_metaObject->GetMetadata();
		wxASSERT(metaData);
		IModuleManager *moduleManager = metaData->GetModuleManager();
		wxASSERT(moduleManager);

		IModuleInfo *dataRef = NULL;

		if (moduleManager->FindCompileModule(m_metaObject, dataRef))
		{
			CCompileModule *compileModule = dataRef->GetCompileModule();

			try
			{
				if (!compileModule->Compile()) {
					wxASSERT("CCompileModule::Compile return false");
				}
			}
			catch (...)
			{
				int answer = wxMessageBox(
					_("Errors were found while checking module. Do you want to continue ?"), compileModule->GetModuleName(),
					wxYES_NO | wxCENTRE);

				if (answer == wxNO)
					return false;
			}
		}
	}

	return CDocument::Save();
}

// ----------------------------------------------------------------------------
// CTextEditDocument implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(CFormEditDocument, CDocument);

CFormEditView* CFormEditDocument::GetFormDesigner() const
{
	wxView* view = GetFirstView();
	return view ? wxDynamicCast(view, CFormEditView) : NULL;
}

void CFormEditDocument::SetCurrentLine(int lineBreakpoint, bool setBreakpoint)
{
	CFormEditView *m_designerForm = GetFormDesigner();
	wxASSERT(m_designerForm);
	m_designerForm->ActivateEditor();

	CCodeEditorCtrl *autoComplete = m_designerForm->GetCodeCtrl();
	autoComplete->SetCurrentLine(lineBreakpoint, setBreakpoint);
}