#include "valueFileDialog.h"
#include "methods.h"

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

CMethods CValueFileDialog::m_methods;

enum {
	enChoose
};

void CValueFileDialog::PrepareNames() const
{
	m_methods.AppendConstructor("fileDialog", "fileDialog(fileDialogMode)");

	m_methods.AppendAttribute("checkFileExist");
	//m_methods.AppendAttribute("defaultExt");
	m_methods.AppendAttribute("directory");
	m_methods.AppendAttribute("filter");
	m_methods.AppendAttribute("filterIndex");
	m_methods.AppendAttribute("fullFileName");
	m_methods.AppendAttribute("mode");
	m_methods.AppendAttribute("multiselect");
	m_methods.AppendAttribute("preview");
	m_methods.AppendAttribute("selectedFiles");
	m_methods.AppendAttribute("title");

	m_methods.AppendMethod("choose", "choose()");
}

#include "appData.h"

CValue CValueFileDialog::Method(methodArg_t &aParams)
{
	if (appData->DesignerMode())
		return CValue();

	switch (aParams.GetIndex())
	{
	case enChoose:
		if (m_dialogMode == eFileDialogMode::eChooseDirectory) {
			return m_dirDialog->ShowModal() != wxID_CANCEL;
		}
		return m_fileDialog->ShowModal() != wxID_CANCEL;
	}

	return CValue();
}

#include "valueArray.h"

void CValueFileDialog::SetAttribute(attributeArg_t &aParams, CValue &cVal)
{
	long style = m_fileDialog->GetWindowStyle();
	long styleDir = m_dirDialog->GetWindowStyle();

	switch (aParams.GetIndex())
	{
	case enCheckFileExist: {
		if (cVal.GetBoolean()) {
			style |= wxFD_FILE_MUST_EXIST;
			styleDir |= wxDD_DIR_MUST_EXIST;
		}
		else {
			style &= (~wxFD_FILE_MUST_EXIST);
			style &= (~wxDD_DIR_MUST_EXIST);
		} 
		break;
	}
	case enDirectory: m_fileDialog->SetDirectory(cVal.ToString());
		m_dirDialog->SetPath(cVal.ToString());
		break;
	case enFilter: m_fileDialog->SetWildcard(cVal.ToString()); break;
	case enFilterIndex: m_fileDialog->SetFilterIndex(cVal.ToInt()); break;
	case enFullFileName: m_fileDialog->SetFilename(cVal.ToString()); break;
	case enMode: {
		m_dialogMode = cVal.ConvertToEnumType<eFileDialogMode>();
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
		if (cVal.GetBoolean()) {
			style |= wxFD_MULTIPLE;
			styleDir |= wxDD_MULTIPLE;
		}
		else {
			style &= (~wxFD_MULTIPLE);
			styleDir &= (~wxDD_MULTIPLE);
		} 
		break;
	}
	case enPreview: if (cVal.GetBoolean()) style |= wxFD_PREVIEW; else style &= (~wxFD_PREVIEW); break;
	case enSelectedFiles: {
		break;
	}
	case enTitle: 
		m_fileDialog->SetMessage(cVal.GetString());
		m_dirDialog->SetMessage(cVal.GetString());
		break;
	}

	m_fileDialog->SetWindowStyle(style);
	m_dirDialog->SetWindowStyle(styleDir);
}

CValue CValueFileDialog::GetAttribute(attributeArg_t &aParams)
{
	long style = m_fileDialog->GetWindowStyle();

	switch (aParams.GetIndex())
	{
	case enCheckFileExist: return (style & wxFD_FILE_MUST_EXIST) != 0; break;
	case enDirectory: 
		if (m_dialogMode == eFileDialogMode::eChooseDirectory) 
			return m_dirDialog->GetPath(); 
		return m_fileDialog->GetDirectory(); 
	break;
	case enFilter: return m_fileDialog->GetWildcard(); break;
	case enFilterIndex: return m_fileDialog->GetFilterIndex(); break;
	case enFullFileName: return m_fileDialog->GetPath(); break;
	case enMode: return new CValueEnumFileDialogMode(m_dialogMode); break;
	case enMultiselect: return (style & wxFD_MULTIPLE) != 0; break;
	case enPreview: return (style & wxFD_PREVIEW) != 0; break;
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
		return new CValueArray(paths);
	}
	case enTitle: return m_fileDialog->GetMessage(); break;
	}

	return CValue();
}

#include "frontend/mainFrame.h"

CValueFileDialog::CValueFileDialog() : CValue(eValueTypes::TYPE_VALUE), m_dialogMode(eFileDialogMode::eOpen),
m_dirDialog(new wxDirDialog(CMainFrame::Get())), m_fileDialog(new wxFileDialog(CMainFrame::Get()))
{
}

CValueFileDialog::~CValueFileDialog()
{
	wxDELETE(m_dirDialog);
	wxDELETE(m_fileDialog);
}

bool CValueFileDialog::Init(CValue **aParams)
{
	m_dialogMode = aParams[0]->
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

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_REGISTER(CValueFileDialog, "fileDialog", TEXT2CLSID("VL_FDIA"));
//add new enumeration
ENUM_REGISTER(CValueEnumFileDialogMode, "fileDialogMode", TEXT2CLSID("EN_FDIM"));