#ifndef __TABLE_BOX_H__
#define __TABLE_BOX_H__

#include "backend/tableInfo.h"

#include "frontend/visualView/ctrl/window.h"
#include "frontend/visualView/ctrl/typeControl.h"

#include "frontend/win/ctrls/tableView.h"

//********************************************************************************************
//*                                 define commom clsid									     *
//********************************************************************************************

//COMMON TABLE & COLUMN
const ibClassID g_controlTableBoxCLSID = string_to_clsid("CT_TABL");
const ibClassID g_controlTableBoxColumnCLSID = string_to_clsid("CT_TBLC");

//********************************************************************************************
//*                                 Value TableBox                                           *
//********************************************************************************************

class ibValueEnumTableBoxSelectionMode :
	public ibValueEnumeration<ibDataViewSelectionMode> {
public:
	ibValueEnumTableBoxSelectionMode() : ibValueEnumeration() {}
	virtual void CreateEnumeration() {
		AddEnumeration(ibDataViewSelectionMode::ibDataViewSelectCell, wxT("SelectCell"), _("Select cell"));
		AddEnumeration(ibDataViewSelectionMode::ibDataViewSelectRow, wxT("SelectRow"), _("Select row"));
	}
private:
	wxDECLARE_DYNAMIC_CLASS(ibValueEnumTableBoxSelectionMode);
};

class ibValueEnumTableBoxViewMode :
	public ibValueEnumeration<ibDataViewViewMode> {
public:
	ibValueEnumTableBoxViewMode() : ibValueEnumeration() {}
	virtual void CreateEnumeration() {
		AddEnumeration(ibDataViewViewMode::ibDataViewHierarchical, wxT("Hierarchical"), _("Hierarchical"));
		AddEnumeration(ibDataViewViewMode::ibDataViewTree, wxT("Tree"), _("Tree"));
		AddEnumeration(ibDataViewViewMode::ibDataViewList, wxT("List"), _("List"));
	}
private:
	wxDECLARE_DYNAMIC_CLASS(ibValueEnumTableBoxViewMode);
};

class ibValueModelTableBox : public ibValueWindow,
	public ibTypeControlFactory, public ibSourceObject {
	wxDECLARE_DYNAMIC_CLASS(ibValueModelTableBox);
public:

	////////////////////////////////////////////////////////////////////////////////////////
	void SetSource(const ibMetaID& id) { m_propertySource->SetValue(id); ibValueModelTableBox::RefreshModel(true); }
	ibMetaID GetSource() const { return m_propertySource->GetValueAsSource(); }
	////////////////////////////////////////////////////////////////////////////////////////

	ibValueModelTableBox();
	virtual ~ibValueModelTableBox() {}

	//Get source attribute  
	virtual ibValueMetaObjectAttributeBase* GetSourceAttributeObject() const { return m_propertySource->GetSourceAttributeObject(); }
	virtual ibSelectorDataType GetFilterDataType() const { return ibSelectorDataType::ibSelectorDataType_table; }
	virtual ibSourceDataType GetFilterSourceDataType() const { return ibSourceDataType::ibSourceDataType_table; }

	//Get source object 
	virtual ibSourceObject* GetSourceObject() const;

#pragma region _source_data_

	//get metaData from object 
	virtual ibValueMetaObjectCompositeData* GetSourceMetaObject() const;
	//get ref class 
	virtual ibClassID GetSourceClassType() const;
	//Get presentation 
	virtual wxString GetSourceCaption() const { return GetString(); }

#pragma endregion 

	//get form owner 
	virtual ibValueForm* GetOwnerForm() const { return m_formOwner; }

	//get model 
	ibValueModel* GetModel() const { return m_tableModel; }

	//get metaData
	virtual ibMetaData* GetMetaData() const;

	//get type description 
	virtual ibTypeDescription& GetTypeDesc() const {
		return m_propertySource->GetValueAsTypeDesc();
	}

	//methods & attributes
	virtual void PrepareNames() const;                         // this method is automatically called to initialize attribute and method names.

	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);        //setting attribute
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);                   //attribute value

	// before/after run 
	virtual bool InitializeControl() { CreateModel(); return true; }

	//get title
	virtual wxString GetControlTitle() const {

		if (!m_propertySource->IsEmptyProperty()) {
			ibValue pvarPropVal;
			if (m_propertySource->GetDataValue(pvarPropVal))
				return _("TableBox") + wxT(": ") + stringUtils::GenerateSynonym(pvarPropVal.GetString());
		}

		return _("TableBox") + wxT(": ") + _("<empty source>");
	}

	//control factory 
	virtual wxObject* Create(wxWindow* wxparent, ibVisualHost* visualHost) override;
