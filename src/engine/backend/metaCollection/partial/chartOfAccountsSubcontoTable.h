#ifndef __CHART_OF_ACCOUNTS_SUBCONTO_TABLE_H__
#define __CHART_OF_ACCOUNTS_SUBCONTO_TABLE_H__

#include "backend/metaCollection/table/metaTableObject.h"

//********************************************************************************************
//*       Predefined tabular section "SubcontoKinds" for Chart of Accounts                   *
//*       Inherits from ibValueMetaObjectTableData, adds predefined columns                  *
//********************************************************************************************

class ibValueMetaObjectSubcontoKindsTable : public ibValueMetaObjectTableData {
	wxDECLARE_DYNAMIC_CLASS(ibValueMetaObjectSubcontoKindsTable);
public:

	ibValueMetaObjectSubcontoKindsTable(const wxString& name, const wxString& synonym, const wxString& comment = wxEmptyString);
	ibValueMetaObjectSubcontoKindsTable();
	virtual ~ibValueMetaObjectSubcontoKindsTable();

	// Accessors for predefined columns
	ibValueMetaObjectAttributePredefined* GetSubcontoKind() const { return m_propertySubcontoKind->GetMetaObject(); }
	ibValueMetaObjectAttributePredefined* GetOrder() const { return m_propertyOrder->GetMetaObject(); }
	ibValueMetaObjectAttributePredefined* GetSummaryOnly() const { return m_propertySummaryOnly->GetMetaObject(); }

	//events
	virtual bool OnCreateMetaObject(ibMetaData* metaData, int flags);
	virtual bool OnLoadMetaObject(ibMetaData* metaData);
	virtual bool OnSaveMetaObject(int flags);
	virtual bool OnDeleteMetaObject();

	virtual bool OnBeforeRunMetaObject(int flags);
	virtual bool OnAfterRunMetaObject(int flags);
	virtual bool OnBeforeCloseMetaObject();
	virtual bool OnAfterCloseMetaObject();

protected:

	virtual bool FillArrayObjectByPredefinedAttribute(std::vector<ibValueMetaObjectAttributeBase*>& array) const override {
		// Call base to get NumberLine
		ibValueMetaObjectTableData::FillArrayObjectByPredefinedAttribute(array);
		// Add our predefined columns
		array.push_back(m_propertySubcontoKind->GetMetaObject());
		array.push_back(m_propertyOrder->GetMetaObject());
		array.push_back(m_propertySummaryOnly->GetMetaObject());
		return true;
	}

	//load & save metaData from DB
	virtual bool LoadData(ibReaderMemory& reader);
	virtual bool SaveData(ibWriterMemory& writer);

private:

	ibPropertyCategory* m_categorySubconto = ibPropertyObject::CreatePropertyCategory(wxT("Subconto"), _("Subconto"));

	// Predefined columns
	ibPropertyContainer<>* m_propertySubcontoKind = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categorySubconto,
		ibValueMetaObjectCompositeData::CreateEmptyType(wxT("SubcontoKind"), _("Subconto kind"), wxEmptyString, false, ibItemMode::ibItemMode_Item));

	ibPropertyContainer<>* m_propertyOrder = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categorySubconto,
		ibValueMetaObjectCompositeData::CreateNumber(wxT("Order"), _("Order"), wxEmptyString, 3, 0, ibItemMode::ibItemMode_Item));

	ibPropertyContainer<>* m_propertySummaryOnly = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categorySubconto,
		ibValueMetaObjectCompositeData::CreateBoolean(wxT("SummaryOnly"), _("Summary only"), wxEmptyString, ibItemMode::ibItemMode_Item));
};

#endif
