#include "htmlbox.h"
#include "compiler/methods.h"

//***********************************************************************************
//*                           IMPLEMENT_DYNAMIC_CLASS                               *
//***********************************************************************************

wxIMPLEMENT_DYNAMIC_CLASS(CValueHTMLBox, IValueWindow);

//***********************************************************************************
//*                                 Value Notebook                                  *
//***********************************************************************************

CValueHTMLBox::CValueHTMLBox() : IValueWindow()
{
	PropertyContainer *categoryNotebook = IObjectBase::CreatePropertyContainer("HTML");
	categoryNotebook->AddProperty("name", PropertyType::PT_WXNAME);
	m_category->AddCategory(categoryNotebook);

	m_minimum_size = wxSize(250, 150);
}

enum
{
	enSetPage = 0,
};

void CValueHTMLBox::PrepareNames() const //этот метод автоматически вызывается для инициализации имен атрибутов и методов
{
	IValueFrame::PrepareNames();

	std::vector<SEng> aMethods =
	{
		{"setPage", "setPage(string)"}
	};

	m_methods->PrepareMethods(aMethods.data(), aMethods.size());
}

CValue CValueHTMLBox::Method(methodArg_t &aParams)       //вызов метода
{
	wxHtmlWindow *htmlBox = dynamic_cast<wxHtmlWindow *>(GetWxObject());

	switch (aParams.GetIndex())
	{
	case enSetPage: return htmlBox ? htmlBox->SetPage(aParams[0].ToString()) : !aParams[0].ToString().IsEmpty();
	}

	return IValueFrame::Method(aParams);
}

wxObject* CValueHTMLBox::Create(wxObject* parent, IVisualHost *visualHost)
{
	wxHtmlWindow *htmlBox = new wxHtmlWindow((wxWindow*)parent, wxID_ANY,
		m_pos,
		m_size);

	wxString dummy_page(
		wxT("<b>wxHtmlWindow</b><br />")
		wxT("This is a dummy page.</body></html>"));

	htmlBox->SetPage(dummy_page);

	return htmlBox;
}

void CValueHTMLBox::OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost *visualHost, bool firstСreated)
{
	wxHtmlWindow *htmlBox = dynamic_cast<wxHtmlWindow *>(wxobject);
}

void CValueHTMLBox::OnSelected(wxObject* wxobject)
{
}

void CValueHTMLBox::Update(wxObject* wxobject, IVisualHost *visualHost)
{
	wxHtmlWindow *htmlBox = dynamic_cast<wxHtmlWindow *>(wxobject);

	if (htmlBox)
	{
	}

	UpdateWindow(htmlBox);
}

void CValueHTMLBox::Cleanup(wxObject* obj, IVisualHost *visualHost)
{
}

//**********************************************************************************
//*                                   Data										   *
//**********************************************************************************

bool CValueHTMLBox::LoadData(CMemoryReader &reader)
{
	return IValueWindow::LoadData(reader);
}

bool CValueHTMLBox::SaveData(CMemoryWriter &writer)
{
	return IValueWindow::SaveData(writer);
}

//**********************************************************************************
//*                                   Property                                     *
//**********************************************************************************


void CValueHTMLBox::ReadProperty()
{
	IValueWindow::ReadProperty();

	IObjectBase::SetPropertyValue("name", m_controlName);
}

void CValueHTMLBox::SaveProperty()
{
	IValueWindow::SaveProperty();

	IObjectBase::GetPropertyValue("name", m_controlName);
}