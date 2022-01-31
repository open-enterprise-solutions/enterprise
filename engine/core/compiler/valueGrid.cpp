////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : simple grid
////////////////////////////////////////////////////////////////////////////

#include "valuegrid.h"
#include "methods.h"
#include "utils/stringUtils.h"

#include "frontend/grid/gridCommon.h"

wxIMPLEMENT_DYNAMIC_CLASS(CValueGrid, CValue);

CMethods CValueGrid::m_methods;

CValueGrid::CValueGrid() : CValue(eValueTypes::TYPE_VALUE) {}

CValueGrid::~CValueGrid() {}

enum
{
	enShowGrid = 0,
};

void CValueGrid::PrepareNames() const
{
	SEng aMethods[] =
	{
		{"showGrid","showGrid()"},
	};

	int nCountM = sizeof(aMethods) / sizeof(aMethods[0]);
	m_methods.PrepareMethods(aMethods, nCountM);
}

void CValueGrid::SetAttribute(attributeArg_t &aParams, CValue &cVal)
{
}

CValue CValueGrid::GetAttribute(attributeArg_t &aParams)
{
	return CValue();
}

#include "common/reportManager.h"

CValue CValueGrid::Method(methodArg_t &aParams)
{
	CValue ret;

	switch (aParams.GetIndex())
	{
	case enShowGrid: ShowGrid(aParams[0].GetType() == eValueTypes::TYPE_EMPTY ? reportManager->MakeNewDocumentName() : aParams[0].ToString()); break;
	}

	return ret;
}

#include "common/cmdProc.h"
#include "frontend/mainFrame.h"
#include "frontend/mainFrameChild.h"
#include "common/templates/template.h"
#include "common/reportManager.h"

bool CValueGrid::ShowGrid(const wxString &sTitle)
{
	CGridEditDocument *m_document = new CGridEditDocument();

	reportManager->AddDocument(m_document);

	m_document->SetCommandProcessor(m_document->CreateCommandProcessor());

	m_document->SetTitle(sTitle);
	m_document->SetFilename(sTitle, true);

	wxScopedPtr<CGridEditView> view(new CGridEditView());
	if (!view) return false;

	view->SetDocument(m_document);

	// create a child valueForm of appropriate class for the current mode
	CDocChildFrame *subvalueFrame = new CDocChildFrame(m_document, view.get(), CMainFrame::Get(), wxID_ANY, sTitle, wxDefaultPosition, wxDefaultSize, wxDEFAULT_FRAME_STYLE);
	subvalueFrame->SetExtraStyle(wxWS_EX_BLOCK_EVENTS);

	if (view->OnCreate(m_document, wxDOC_NEW)) { view->ShowFrame(); }
	return view.release() != NULL;
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_REGISTER(CValueGrid, "tableDocument", TEXT2CLSID("VL_GRID"));