#ifndef _OBJECT_LIST_H__
#define _OBJECT_LIST_H__

#include "metadata/objects/baseObject.h"

class IDataObjectList : public IValueTable,
	public IDataObjectSource {
	wxDECLARE_ABSTRACT_CLASS(IDataObjectList);
private:

	void Prepend(const wxString &text);
	void DeleteItem(const wxDataViewItem &item);
	void DeleteItems(const wxDataViewItemArray &items);

	// implementation of base class virtuals to define model
	virtual unsigned int GetColumnCount() const override;
	virtual wxString GetColumnType(unsigned int col) const override;

	virtual void GetValueByRow(wxVariant &variant,
		unsigned int row, unsigned int col) const;

	virtual bool GetAttrByRow(unsigned int row, unsigned int col, wxDataViewItemAttr &attr) const;

	virtual bool SetValueByRow(const wxVariant &variant,
		unsigned int row, unsigned int col) override;

	virtual IValueTableColumns *GetColumns() const override { return NULL; }

	virtual IValueTableReturnLine *GetRowAt(unsigned int line)
	{
		if (line > m_aObjectValues.size())
			return NULL;

		return new СDataObjectListReturnLine(this, line);
	};

public:

	class СDataObjectListReturnLine : public IValueTableReturnLine {
		wxDECLARE_DYNAMIC_CLASS(СDataObjectListReturnLine);
	public:
		IDataObjectList *m_ownerTable; int m_lineTable;
	public:

		virtual unsigned int GetLineTable() const { return m_lineTable; }
		virtual IValueTable *GetOwnerTable() const { return m_ownerTable; }

		СDataObjectListReturnLine();
		СDataObjectListReturnLine(IDataObjectList *ownerTable, int line);
		virtual ~СDataObjectListReturnLine();

		virtual CMethods* GetPMethods() const { PrepareNames(); return m_methods; }; //получить ссылку на класс помощник разбора имен атрибутов и методов
		virtual void PrepareNames() const;                         //этот метод автоматически вызывается для инициализации имен атрибутов и методов

		virtual wxString GetTypeString() const { return wxT("listValueRow"); }
		virtual wxString GetString() const { return wxT("listValueRow"); }

		virtual void SetAttribute(attributeArg_t &aParams, CValue &cVal); //установка атрибута
		virtual CValue GetAttribute(attributeArg_t &aParams); //значение атрибута

	private:
		CMethods *m_methods;
	};

public:

	//Работа с итераторами 
	virtual bool HasIterator() const override { return true; }

	virtual CValue GetItEmpty() override
	{
		return new СDataObjectListReturnLine(this, wxNOT_FOUND);
	}

	virtual CValue GetItAt(unsigned int idx) override
	{
		if (idx > m_aObjectValues.size())
			return CValue();

		return new СDataObjectListReturnLine(this, idx);
	}

	virtual unsigned int GetItSize() const override { return m_aObjectValues.size(); }

	virtual bool AutoCreateColumns() { return false; }
	virtual bool EditableLine(const wxDataViewItem &item, unsigned int col) { return false; }

	virtual wxDataViewItem GetLineByGuid(const Guid &guid) const;

	//ctor
	IDataObjectList(IMetaObjectRefValue *metaObject = NULL, form_identifier_t formType = wxNOT_FOUND);

	//****************************************************************************
	//*                               Support model                              *
	//****************************************************************************

	virtual void UpdateModel();

	//get metadata from object 
	virtual IMetaObjectValue *GetMetaObject() const { return m_metaObject; };

	//get unique identifier 
	virtual Guid GetGuid() const { return m_objGuid; };

	//counter
	virtual void IncrRef() { CValue::IncrRef(); }
	virtual void DecrRef() { CValue::DecrRef(); }

	virtual inline bool IsEmpty() const { return false; }

	//support source data 
	virtual CSourceExplorer GetSourceExplorer() const;
	virtual bool GetTable(IValueTable *&tableValue, meta_identifier_t id);

	//update data 
	virtual void UpdateData() { UpdateModel(); }

	//Get ref class 
	virtual CLASS_ID GetTypeID() const;

	virtual wxString GetTypeString() const;
	virtual wxString GetString() const;

	//operator 
	virtual operator CValue() const { return this; };

protected:

	IMetaObjectRefValue *m_metaObject;

	Guid m_objGuid;
	std::map<Guid, std::map<meta_identifier_t, CValue>> m_aObjectValues;
};

class CDataObjectList : public IDataObjectList {
	wxDECLARE_DYNAMIC_CLASS(CDataObjectList);
public:
	//Constructor
	CDataObjectList(IMetaObjectRefValue *metaObject = NULL, form_identifier_t formType = wxNOT_FOUND, bool choiceMode = false);
	virtual ~CDataObjectList();

	//****************************************************************************
	//*                              Support methods                             *
	//****************************************************************************
	virtual CMethods* GetPMethods() const;                          // получить ссылку на класс помощник разбора имен атрибутов и методов
	virtual void PrepareNames() const;                             // этот метод автоматически вызывается для инициализации имен атрибутов и методов
	virtual CValue Method(methodArg_t &aParams);       // вызов метода

	//****************************************************************************
	//*                              Override attribute                          *
	//****************************************************************************
	virtual void SetAttribute(attributeArg_t &aParams, CValue &cVal);        //установка атрибута
	virtual CValue GetAttribute(attributeArg_t &aParams);                   //значение атрибута

	//on activate item
	virtual void ActivateItem(CValueForm *srcForm,
		const wxDataViewItem &item, unsigned int col) {
		if (m_bChoiceMode) {
			ChooseValue(srcForm);
		}
		else {
			EditValue(); 
		}
	}

	//support actions
	virtual actionData_t GetActions(form_identifier_t formType);
	virtual void ExecuteAction(action_identifier_t action, CValueForm *srcForm);

	//events:
	virtual void AddValue(unsigned int before = 0) override;
	virtual void CopyValue() override;
	virtual void EditValue() override;
	virtual void DeleteValue() override;

	virtual void ChooseValue(CValueForm *srcForm);

private:
	bool m_bChoiceMode; 
	CMethods *m_methods;
};

#endif 