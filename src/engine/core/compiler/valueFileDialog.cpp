#include "valueFileDialog.h"

wxIMPLEMENT_DYNAMIC_CLASS(CValueFileDialog, CValue);
wxIMPLEMENT_DYNAMIC_CLASS(CValueEnumFileDialogMode, CValue);

enum {
	enCheckFileExist,
	//enDefaultExt,
	enDirectory,
	enFilter,
	enFilterIndex,
	enFullFileName,
	enMode,
	enMultiselect,
	enPreview,
	enSelectedFiles,
	enTitle
};

CValue::CMethodHelper CValueFileDialog::m_methodHelper;

#include "valueArray.h"
#include "frontend/mainFrame.h"

CValueFileDialog::CValueFileDialog() : CValue(eValueTypes::TYPE_VALUE), m_dialogMode(eFileDialogMode::eOpen),
m_dirDialog(new wxDirDialog(wxAuiDocMDIFrame::GetFrame())), m_fileDialog(new wxFileDialog(wxAuiDocMDIFrame::GetFrame()))
{
}

CValueFileDialog::~CValueFileDialog()
{
	wxDELETE(m_dirDialog);
	wxDELETE(m_fileDialog);
}

bool CValueFileDialog::Init(CValue **paParams, const long lSizeArray)
{
	m_dialogMode = paParams[0]->
		ConvertToEnumType<eFileDialogMode>();

	int style = m_fileDialog->GetWindowStyle();

	if (m_dialogMode == eFileDialogMode::eOpen) {
		style |= wxFD_OPEN;
		style &= (~wxFD_SAVE);
	}
	else if (m_dialogMode == eFileDialogMode::eSave) {
		style |= (wxFD_SAVE);
		style &= (~wxFD_OPEN);
	}
	else {
		style &= (~wxFD_OPEN);
		style &= (wxFD_SAVE);
	}

	m_fileDialog->SetWindowStyle(style);
	return true;
}

enum {
	enChoose
};

void CValueFileDialog::PrepareNames() const
{
	m_methodHelper.ClearHelper();
	m_methodHelper.AppendConstructor(1, "fileDialog(fileDialogMode)");
	m_methodHelper.AppendProp("checkFileExist");
	//m_methodHelper.AppendProp("defaultExt");
	m_methodHelper.AppendProp("directory");
	m_methodHelper.AppendProp("filter");
	m_methodHelper.AppendProp("filterIndex");
	m_methodHelper.AppendProp("fullFileName");
	m_methodHelper.AppendProp("mode");
	m_methodHelper.AppendProp("multiselect");
	m_methodHelper.AppendProp("preview");
	m_methodHelper.AppendProp("selectedFiles");
	m_methodHelper.AppendProp("title");
	m_methodHelper.AppendFunc("choose", "choose()");
}

bool CValueFileDialog::SetPropVal(const long lPropNum, const CValue& varPropVal)
{
	long style = m_fileDialog->GetWindowStyle();
	long styleDir = m_dirDialog->GetWindowStyle();

	switch (lPropNum)
	{
	case enCheckFileExist: {
		if (varPropVal.GetBoolean()) {
			style |= wxFD_FILE_MUST_EXIST;
			styleDir |= wxDD_DIR_MUST_EXIST;
		}
		else {
			style &= (~wxFD_FILE_MUST_EXIST);
			style &= (~wxDD_DIR_MUST_EXIST);
		}
		break;
	}
	case enDirectory: m_fileDialog->SetDirectory(varPropVal.GetString());
		m_dirDialog->SetPath(varPropVal.GetString());
		break;
	case enFilter: m_fileDialog->SetWildcard(varPropVal.GetString()); break;
	case enFilterIndex: m_fileDialog->SetFilterIndex(varPropVal.GetInteger()); break;
	case enFullFileName: m_fileDialog->SetFilename(varPropVal.GetString()); break;
	case enMode: {
		m_dialogMode = varPropVal.ConvertToEnumType<eFileDialogMode>();
		if (m_dialogMode == eFileDialogMode::eOpen) {
			style |= wxFD_OPEN;
			style &= (~wxFD_SAVE);
		}
		else if (m_dialogMode == eFileDialogMode::eSave) {
			style |= (wxFD_SAVE);
			style &= (~wxFD_OPEN);
		}
		else {
			style &= (~wxFD_OPEN);
			style &= (wxFD_SAVE);
		}
		break;
	}
	case enMultiselect: {
		if (varPropVal.GetBoolean()) {
			style |= wxFD_MULTIPLE;
			styleDir |= wxDD_MULTIPLE;
		}
		else {
			style &= (~wxFD_MULTIPLE);
			styleDir &= (~wxDD_MULTIPLE);
		}
		break;
	}
	case enPreview: if (varPropVal.GetBoolean()) style |= wxFD_PREVIEW; else style &= (~wxFD_PREVIEW); break;
	case enSelectedFiles: {
		break;
	}
	case enTitle:
		m_fileDialog->SetMessage(varPropVal.GetString());
		m_dirDialog->SetMessage(varPropVal.GetString());
		break;
	}

	m_fileDialog->SetWindowStyle(style);
	m_dirDialog->SetWindowStyle(styleDir);
	return true;
}

bool CValueFileDialog::GetPropVal(const long lPropNum, CValue& pvarPropVal)
{
	long style = m_fileDialog->GetWindowStyle();

	switch (lPropNum)
	{
	case enCheckFileExist: 
		pvarPropVal = (style & wxFD_FILE_MUST_EXIST) != 0; 
		return true;
	case enDirectory:
		if (m_dialogMode == eFileDialogMode::eChooseDirectory)
			pvarPropVal = m_dirDialog->GetPath();
		pvarPropVal = m_fileDialog->GetDirectory();
		return true;
	case enFilter: 
		pvarPropVal = m_fileDialog->GetWildcard();
		return true;
	case enFilterIndex: 
		pvarPropVal = m_fileDialog->GetFilterIndex();
		return true;
	case enFullFileName: 
		pvarPropVal = m_fileDialog->GetPath();
		return true;
	case enMode: 
		pvarPropVal = new CValueEnumFileDialogMode(m_dialogMode); 
		return true;
	case enMultiselect: 
		pvarPropVal = (style & wxFD_MULTIPLE) != 0; 
		return true;
	case enPreview:
		pvarPropVal = (style & wxFD_PREVIEW) != 0; 
		return true;
	case enSelectedFiles: {
		wxArrayString arrString;
		if (m_dialogMode == eFileDialogMode::eChooseDirectory) {
			m_dirDialog->GetPaths(arrString);
		}
		else {
			m_fileDialog->GetPaths(arrString);
		}
		std::vector < CValue> paths;
		for (auto path : arrString) {
			paths.push_back(path);
		}
		pvarPropVal = new CValueArray(paths);
		return true;
	}
	case enTitle:
		pvarPropVal = m_fileDialog->GetMessage();
		return true;
	}

	return false;
}

#include "appData.h"

bool CValueFileDialog::CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray)
{
	if (appData->DesignerMode())
		return false;

	switch (lMethodNum)
	{
	case enChoose:
		if (m_dialogMode == eFileDialogMode::eChooseDirectory) {
			pvarRetValue = m_dirDialog->ShowModal() != wxID_CANCEL;
			return true;
		}
		pvarRetValue = m_fileDialog->ShowModal() != wxID_CANCEL;
		return true; 
	}

	return false;
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_REGISTER(CValueFileDialog, "fileDialog", TEXT2CLSID("VL_FDIA"));
//add new enumeration
ENUM_REGISTER(CValueEnumFileDialogMode, "fileDialogMode", TEXT2CLSID("EN_FDIM"));