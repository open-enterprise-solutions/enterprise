////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : simple grid
////////////////////////////////////////////////////////////////////////////

#include "valuegrid.h"
#include "utils/stringUtils.h"

#include "frontend/grid/gridCommon.h"

wxIMPLEMENT_DYNAMIC_CLASS(CValueGrid, CValue);

CValue::CMethodHelper CValueGrid::m_methodHelper;

CValueGrid::CValueGrid() : 
	CValue(eValueTypes::TYPE_VALUE) {
}

CValueGrid::~CValueGrid() {
}

void CValueGrid::PrepareNames() const
{
	m_methodHelper.ClearHelper();
	m_methodHelper.AppendFunc(wxT("showGrid"), "showGrid()");
}

bool CValueGrid::SetPropVal(const long lPropNum, CValue &varPropVal)
{
	return false;
}

bool CValueGrid::GetPropVal(const long lPropNum, CValue& pvarPropVal)
{
	return false;
}

#include "core/frontend/docView/docManager.h"

bool CValueGrid::CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case enShowGrid: 
		ShowGrid(
			paParams[0]->GetType() == eValueTypes::TYPE_EMPTY ? docManager->MakeNewDocumentName() : paParams[0]->GetString()
		); 
		return true;
	}
	return false;
}

#include "frontend/mainFrame.h"
#include "frontend/mainFrameChild.h"
#include "core/frontend/docView/templates/template.h"
#include "core/frontend/docView/docManager.h"

bool CValueGrid::ShowGrid(const wxString &sTitle)
{
	CGridEditDocument *m_document = new CGridEditDocument();

	docManager->AddDocument(m_document);

	m_document->SetCommandProcessor(m_document->CreateCommandProcessor());

	m_document->SetTitle(sTitle);
	m_document->SetFilename(sTitle, true);

	wxScopedPtr<CGridEditView> view(new CGridEditView());
	if (!view) return false;

	view->SetDocument(m_document);

	// create a child valueForm of appropriate class for the current mode
	CMDIDocChildFrame *subvalueFrame = new CMDIDocChildFrame(m_document, view.get(), CDocMDIFrame::GetFrame(), wxID_ANY, sTitle, wxDefaultPosition, wxDefaultSize, wxDEFAULT_FRAME_STYLE);
	subvalueFrame->SetExtraStyle(wxWS_EX_BLOCK_EVENTS);

	if (view->OnCreate(m_document, wxDOC_NEW)) { view->ShowFrame(); }
	return view.release() != NULL;
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_TYPE_REGISTER(CValueGrid, "tableDocument", TEXT2CLSID("VL_GRID"));