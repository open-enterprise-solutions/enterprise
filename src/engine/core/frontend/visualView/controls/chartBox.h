#ifndef _CHARTBOX_H__
#define _CHARTBOX_H__

#include "window.h"

class CValueChartBox : public IValueWindow
{
	wxDECLARE_DYNAMIC_CLASS(CValueChartBox);

private:

	wxString m_name;

public:

	CValueChartBox();

	//methods 
	virtual void PrepareNames() const;                         //этот метод автоматически вызывается для инициализации имен атрибутов и методов
	virtual CValue Method(methodArg_t &aParams);       //вызов метода

	virtual wxObject* Create(wxObject* parent, IVisualHost *visualHost) override;
	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost *visualHost, bool firstСreated) override;
	virtual void OnSelected(wxObject* wxobject) override;
	virtual void Update(wxObject* wxobject, IVisualHost *visualHost) override;
	virtual void Cleanup(wxObject* obj, IVisualHost *visualHost) override;

	virtual wxString GetClassName() const override { return wxT("chartbox"); }
	virtual wxString GetObjectTypeName() const override { return wxT("container"); }

	//load & save object in control 
	virtual bool LoadData(CMemoryReader &reader);
	virtual bool SaveData(CMemoryWriter &writer = CMemoryWriter());

	//read&save propery 
	virtual void ReadProperty() override;
	virtual void SaveProperty() override;
};

#endif