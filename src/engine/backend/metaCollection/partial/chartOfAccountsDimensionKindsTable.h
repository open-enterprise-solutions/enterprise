#ifndef __CHART_OF_ACCOUNTS_DIMENSION_KINDS_TABLE_H__
#define __CHART_OF_ACCOUNTS_DIMENSION_KINDS_TABLE_H__

#include "backend/metaCollection/table/metaTableObject.h"

//********************************************************************************************
//*       Predefined tabular section "AccountDimensionKinds" for Chart of Accounts           *
//*       Inherits from ibValueMetaObjectTableData, adds predefined columns                  *
//*                                                                                          *
//*       A KIND is an element of the chart of characteristic types; the VALUE it admits is  *
//*       that chart's own composition. Two different things, and the interface says so in   *
//*       two different words — "вид аналитики" and "аналитика".                             *
//********************************************************************************************

// DB-BACKED, therefore ibValueMetaObjectTableDataRef and not the shared base. The base is abstract
// about storage: its GetQueryable() answers null and it has no physical table name, because a RAM
// section (a data processor's) genuinely has neither. This table's rows belong to a SAVED account,
// so it needs the Ref variant's L4 source descriptor — the queryable it vends, the "<Parent><id>_VT<id>"
// table, and the RegisterSource / UnregisterSource pair on run and close.
//
// Deriving from the base compiled and read back fine; it failed only where a row is WRITTEN — the
// write door dereferenced the null queryable, so saving an account with any analytics crashed.
class ibValueMetaObjectAccountDimensionKindsTable : public ibValueMetaObjectTableDataRef {
	public:

	ibValueMetaObjectAccountDimensionKindsTable(const wxString& name, const wxString& synonym, const wxString& comment = wxEmptyString);
	ibValueMetaObjectAccountDimensionKindsTable();
	virtual ~ibValueMetaObjectAccountDimensionKindsTable();

	// Accessors for predefined columns
	ibValueMetaObjectAttributePredefined* GetAccountDimensionKind() const { return m_propertyAccountDimensionKind->GetMetaObject(); }
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

	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

protected:

	virtual bool FillArrayObjectByPredefinedAttribute(std::vector<ibValueMetaObjectAttributeBase*>& array) const override {
		// Call base to get NumberLine
		ibValueMetaObjectTableDataRef::FillArrayObjectByPredefinedAttribute(array);
		// Add our predefined columns
		array.push_back(m_propertyAccountDimensionKind->GetMetaObject());
		array.push_back(m_propertyOrder->GetMetaObject());
		array.push_back(m_propertySummaryOnly->GetMetaObject());
		return true;
	}

	//load & save metaData from DB

private:

	ibPropertyCategory* m_categoryAccountDimension = ibPropertyObject::CreatePropertyCategory(wxT("AccountDimension"), _("Account dimension"));

	// Predefined columns
	// FILL-CHECKED: a line of this table IS a kind. A row with an empty kind carries an order and a
	// flag about nothing — it would reach the register as an analytics slot that admits everything,
	// which is indistinguishable from an account that declares no analytics at all.
	ibPropertyContainer<>* m_propertyAccountDimensionKind = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryAccountDimension,
		ibValueMetaObjectCompositeData::CreateEmptyType(wxT("AccountDimensionKind"), _("Account dimension kind"), wxEmptyString, /*fillCheck*/ true, ibItemMode::ibItemMode_Item));

	ibPropertyContainer<>* m_propertyOrder = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryAccountDimension,
		ibValueMetaObjectCompositeData::CreateNumber(wxT("Order"), _("Order"), wxEmptyString, 3, 0, ibItemMode::ibItemMode_Item));

	ibPropertyContainer<>* m_propertySummaryOnly = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryAccountDimension,
		ibValueMetaObjectCompositeData::CreateBoolean(wxT("SummaryOnly"), _("Summary only"), wxEmptyString, ibItemMode::ibItemMode_Item));
};

#endif
