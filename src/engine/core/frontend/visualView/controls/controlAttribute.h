#ifndef _ATTRIBUTE_CONTROL_H__
#define _ATTRIBUTE_CONTROL_H__

#include "core/compiler/value.h"

class IMetaObjectWrapperData;
class ISourceDataObject;

#include "core/common/typeInfo.h"
#include "core/common/srcObject.h"

class CValueForm;

///////////////////////////////////////////////////////////////////////////

class ITypeControl :
	public ITypeAttribute {
protected:
	virtual void DoSetFromMetaId(const meta_identifier_t& id);
public:

	enum eSelectMode GetSelectMode() const;

	virtual CLASS_ID GetFirstClsid() const;
	virtual std::set<CLASS_ID> GetClsids() const;

	IMetaObject* GetMetaSource() const;
	IMetaObject* GetMetaObjectById(const CLASS_ID& clsid) const;

	void SetSourceId(const meta_identifier_t& id);
	meta_identifier_t GetSourceId() const;

	void ResetSource();

	//ctor
	ITypeControl(const eValueTypes& defType = eValueTypes::TYPE_STRING) :
		ITypeAttribute(defType) {
	}

	ITypeControl(const CLASS_ID& clsid) :
		ITypeAttribute(clsid) {
	}

	ITypeControl(const std::set<CLASS_ID>& clsids) :
		ITypeAttribute(clsids) {
	}

	ITypeControl(const std::set<CLASS_ID>& clsids, const typeDescription_t::typeData_t& descr) :
		ITypeAttribute(clsids, descr) {
	}

	//////////////////////////////////////////////////

	meta_identifier_t GetIdByGuid(const Guid& guid) const;
	Guid GetGuidByID(const meta_identifier_t& id) const;

	//////////////////////////////////////////////////

	virtual bool LoadTypeData(CMemoryReader& dataReader);
	virtual bool LoadFromVariant(const wxVariant& variant);
	virtual bool SaveTypeData(CMemoryWriter& dataWritter);
	virtual void SaveToVariant(wxVariant& variant, IMetadata* metaData) const;

	//////////////////////////////////////////////////

	static bool SimpleChoice(class IControlFrame* ownerValue, const CLASS_ID& clsid, wxWindow* parent);

	static bool QuickChoice(class IControlFrame* ownerValue, const CLASS_ID& clsid, wxWindow* parent);
	static void QuickChoice(class IControlFrame* controlValue, CValue& newValue, wxWindow* parent, const wxString& strData);

	//////////////////////////////////////////////////

	//Get source object 
	virtual ISourceObject* GetSourceObject() const = 0;

	//Get source object 
	virtual CValueForm* GetOwnerForm() const = 0;

	//Get meta object
	virtual IMetaObjectWrapperData* GetMetaObject() const;

	//Create value by selected type
	virtual CValue CreateValue() const;
	virtual CValue* CreateValueRef() const;

	//Get data type 
	virtual CLASS_ID GetDataType() const;

protected:

	Guid m_dataSource;
};

///////////////////////////////////////////////////////////////////////////

class wxVariantSourceAttributeData : public wxVariantAttributeData {
	void UpdateSourceAttribute();
public:
	wxVariantSourceAttributeData(IMetadata* metaData, CValueForm* formData, const meta_identifier_t& id = wxNOT_FOUND)
		: wxVariantAttributeData(metaData), m_formData(formData)
	{
		DoSetFromMetaId(id);
	}

	wxVariantSourceAttributeData(const wxVariantSourceAttributeData& srcData)
		: wxVariantAttributeData(srcData), m_formData(srcData.m_formData)
	{
		UpdateSourceAttribute();
	}

protected:
	virtual void DoSetFromMetaId(const meta_identifier_t& id);
protected:
	CValueForm* m_formData;
};

///////////////////////////////////////////////////////////////////////////

class wxVariantSourceData :
	public wxVariantData {
	wxString MakeString() const;
public:

	wxVariantSourceAttributeData* GetAttributeData() const {
		return m_typeDescription;
	}

	void SetSourceId(const meta_identifier_t& id) {
		m_srcId = id;
	}

	meta_identifier_t GetSourceId() const {
		return m_srcId;
	}

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	void RefreshData() {
		if (m_typeDescription != NULL) {
			m_typeDescription->RefreshData(m_srcId);
		}
	}

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	wxVariantSourceData(IMetadata* metaData, CValueForm* formData, const meta_identifier_t& id = wxNOT_FOUND) : wxVariantData(),
		m_typeDescription(new wxVariantSourceAttributeData(metaData, formData, id)), m_srcId(id), m_formData(formData), m_metaData(metaData) {
	}

	wxVariantSourceData(const wxVariantSourceData& srcData) : wxVariantData(),
		m_typeDescription(new wxVariantSourceAttributeData(*srcData.m_typeDescription)), m_srcId(srcData.m_srcId), m_formData(srcData.m_formData), m_metaData(srcData.m_metaData) {
	}

	virtual ~wxVariantSourceData() {
		wxDELETE(m_typeDescription);
	}

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	virtual bool Eq(wxVariantData& data) const {
		wxVariantSourceData* srcData = dynamic_cast<wxVariantSourceData*>(&data);
		if (srcData != NULL) {
			wxVariantAttributeData* srcAttr = srcData->GetAttributeData();
			return m_srcId == srcData->GetSourceId()
				&& srcAttr->Eq(*m_typeDescription);
		}
		return false;
	}

#if wxUSE_STD_IOSTREAM
	virtual bool Write(wxSTD ostream& str) const {
		str << MakeString();
		return true;
	}
#endif
	virtual bool Write(wxString& str) const {
		str = MakeString();
		return true;
	}

	virtual wxString GetType() const {
		return wxT("wxVariantSourceData");
	}

	CValueForm* GetOwnerForm() const {
		return m_formData;
	}

	IMetadata* GetMetadata() const {
		return m_metaData;
	}

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

protected:

	IMetadata* m_metaData;

	CValueForm* m_formData;
	wxVariantSourceAttributeData* m_typeDescription;

	meta_identifier_t m_srcId;
};

#endif