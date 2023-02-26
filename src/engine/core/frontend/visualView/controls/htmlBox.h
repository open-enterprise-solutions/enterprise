#ifndef  _HTMLBOX_H__

#include "window.h"
#include <wx/html/htmlwin.h>

class CValueHTMLBox : public IValueWindow {
	wxDECLARE_DYNAMIC_CLASS(CValueHTMLBox);
public:

	CValueHTMLBox(); 

	//methods 
	virtual void PrepareNames() const;                         //этот метод автоматически вызывается для инициализации имен атрибутов и методов
	virtual bool CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray);       //вызов метода

	virtual wxObject* Create(wxWindow* wxparent, IVisualHost *visualHost) override;
	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost *visualHost, bool firstСreated) override;
	virtual void OnSelected(wxObject* wxobject) override;
	virtual void Update(wxObject* wxobject, IVisualHost *visualHost) override;
	virtual void Cleanup(wxObject* obj, IVisualHost *visualHost) override;

	virtual wxString GetClassName() const override { return wxT("htmlbox"); }
	virtual wxString GetObjectTypeName() const override { return wxT("container"); }

	//support icons
	virtual wxIcon GetIcon();
	static wxIcon GetIconGroup();

	//load & save object in control 
	virtual bool LoadData(CMemoryReader &reader);
	virtual bool SaveData(CMemoryWriter &writer = CMemoryWriter());
};

#endif // ! _HTMLBOX_H__
