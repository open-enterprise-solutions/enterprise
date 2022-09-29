#ifndef _ATTRIBUTE_CONTROL_H__
#define _ATTRIBUTE_CONTROL_H__

#include "compiler/value.h"
#include "common/attributeInfo.h"

class IMetaObjectWrapperData;
class ISourceDataObject;

#include "guid/guid.h"

class CValueForm;

///////////////////////////////////////////////////////////////////////////

class IAttributeControl :
	public IAttributeInfo {
protected:
	virtual void DoSetFromMetaId(const meta_identifier_t& id);
public:

	virtual CLASS_ID GetFirstClsid() const;
	virtual std::set<CLASS_ID> GetClsids() const;

	IMetaObject* GetMetaSource() const;
	IMetaObject* GetMetaObjectById(const CLASS_ID& clsid) const;

	void SetSourceId(const meta_identifier_t& id);
	meta_identifier_t GetSourceId() const;

	void ResetSource();

	//ctor
	IAttributeControl(const eValueTypes& defType = eValueTypes::TYPE_STRING) :
		IAttributeInfo(defType)
	{
	}

	IAttributeControl(const CLASS_ID& clsid) :
		IAttributeInfo(clsid)
	{
	}

	IAttributeControl(const std::set<CLASS_ID>& clsids) :
		IAttributeInfo(clsids)
	{
	}

	IAttributeControl(const std::set<CLASS_ID>& clsids, const metaDescription_t& descr) :
		IAttributeInfo(clsids, descr)
	{
	}

	//////////////////////////////////////////////////

	meta_identifier_t GetIdByGuid(const Guid &guid) const;
	Guid GetGuidByID(const meta_identifier_t& id) const;

	//////////////////////////////////////////////////

	virtual bool LoadTypeData(CMemoryReader& dataReader);
	virtual bool LoadFromVariant(const wxVariant& variant);
	virtual bool SaveTypeData(CMemoryWriter& dataWritter);
	virtual void SaveToVariant(wxVariant& variant, IMetadata* metaData) const;

	//////////////////////////////////////////////////

	virtual bool SelectSimpleValue(const CLASS_ID &clsid, wxWindow* parent) const;

	//////////////////////////////////////////////////

	//Get source object 
	virtual ISourceDataObject* GetSourceObject() const = 0;

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
		return m_typeData;
	}

	void SetSourceId(const meta_identifier_t& id) {
		m_srcId = id;
	}

	meta_identifier_t GetSourceId() const {
		return m_srcId;
	}

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	void RefreshData() {
		if (m_typeData != NULL) {
			m_typeData->RefreshData(m_srcId);
		}
	}

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	wxVariantSourceData(IMetadata* metaData, CValueForm* formData, const meta_identifier_t& id = wxNOT_FOUND) : wxVariantData(),
		m_typeData(new wxVariantSourceAttributeData(metaData, formData, id)), m_srcId(id), m_formData(formData), m_metaData(metaData) {
	}

	wxVariantSourceData(const wxVariantSourceData& srcData) : wxVariantData(),
		m_typeData(new wxVariantSourceAttributeData(*srcData.m_typeData)), m_srcId(srcData.m_srcId), m_formData(srcData.m_formData), m_metaData(srcData.m_metaData) {
	}

	virtual ~wxVariantSourceData() {
		wxDELETE(m_typeData);
	}

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	virtual bool Eq(wxVariantData& data) const {
		wxVariantSourceData* srcData = dynamic_cast<wxVariantSourceData*>(&data);
		if (srcData != NULL) {
			wxVariantAttributeData* srcAttr = srcData->GetAttributeData();
			return m_srcId == srcData->GetSourceId()
				&& srcAttr->Eq(*m_typeData);
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
	wxVariantSourceAttributeData* m_typeData;

	meta_identifier_t m_srcId;
};

#endif