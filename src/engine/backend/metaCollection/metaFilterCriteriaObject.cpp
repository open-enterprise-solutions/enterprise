#include "metaFilterCriteriaObject.h"
#include "backend/metaCtor.h"
#include "backend/objCtor.h"
#include "backend/backend_exception.h"
#include "core/stringUtils.h"
#include "core/serialize/dataBuilder.h"
#include "backend/metaCollection/attribute/metaAttributeObject.h"
#include "backend/query/dataQueryBuilder.h"
#include "backend/system/value/valueArray.h"
#include <vector>
namespace {
class ibValueFilterCriteriaManager : public ibValueDynamicMembers {
public:
	explicit ibValueFilterCriteriaManager(ibValueMetaObjectFilterCriteria* meta = nullptr)
		: ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, true), m_meta(meta)
	{
		m_members.Bind(this, &ibValueFilterCriteriaManager::Fill);
	}
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray) override;
	virtual wxString GetClassName() const override;
private:
	void Fill(ibMemberTable& helper) const { helper.AppendFunc(wxT("Find"), 1, wxT("Find(value)")); }
	ibValue Find(const ibValue& value) const;
	ibValueMetaObjectFilterCriteria* m_meta;
};
class ibCtorFilterCriteriaManager : public ibCtorMetaValueType {
public:
	explicit ibCtorFilterCriteriaManager(ibValueMetaObjectFilterCriteria* meta) : m_meta(meta) {
		m_classType = manager_to_clsid(meta->GetMetaID(), clsid_metaclass(meta->GetClassType()));
	}
	virtual wxString GetClassName() const override {
		return m_meta->GetClassName() + prefixManager + m_meta->GetName();
	}
	virtual ibClassID GetClassType() const override { return m_classType; }
	virtual ibValue CreateObject() const override { return new ibValueFilterCriteriaManager(m_meta); }
	virtual const ibValueMetaObject* GetMetaObject() const override { return m_meta; }
	virtual ibCtorObjectMetaType GetMetaTypeCtor() const override { return ibCtorObjectMetaType::ibCtorObjectMetaType_Manager; }
private:
	ibClassID m_classType = 0;
	ibValueMetaObjectFilterCriteria* m_meta;
};
ibClassID KindOf(const wxString& token)
{
	if (stringUtils::CompareString(token, wxT("Document")) || stringUtils::CompareString(token, wxT("Documents")))
		return g_metaDocumentCLSID;
	if (stringUtils::CompareString(token, wxT("Catalog")) || stringUtils::CompareString(token, wxT("Catalogs")))
		return g_metaCatalogCLSID;
	return 0;
}
void SplitPath(const wxString& line, std::vector<wxString>& parts)
{
	wxString rest = line;
	while (!rest.IsEmpty()) {
		const bool more = rest.Contains(wxT('.'));
		wxString part = rest.BeforeFirst(wxT('.'));
		rest = more ? rest.AfterFirst(wxT('.')) : wxString();
		part.Trim(true).Trim(false);
		if (!part.IsEmpty())
			parts.push_back(part);
	}
}
ibValueMetaObject* NamedOf(ibMetaData* meta, ibClassID clsid, const wxString& name)
{
	if (meta == nullptr)
		return nullptr;
	for (ibValueMetaObject* object : meta->GetAnyArrayObject(clsid)) {
		if (object != nullptr && stringUtils::CompareString(object->GetName(), name))
			return object;
	}
	return nullptr;
}
ibValueMetaObjectAttributeBase* AttributeNamed(ibValueMetaObject* owner, const wxString& name)
{
	if (owner == nullptr)
		return nullptr;
	for (unsigned int i = 0; i < owner->GetChildCount(); ++i) {
		ibValueMetaObjectAttributeBase* const attr = dynamic_cast<ibValueMetaObjectAttributeBase*>(owner->GetChild(i));
		if (attr != nullptr && stringUtils::CompareString(attr->GetName(), name))
			return attr;
	}
	return nullptr;
}
void SearchOne(ibValueMetaObjectRecordDataRef* owner, ibValueMetaObjectAttributeBase* attr, const ibValue& value, ibValueArray* found)
{
	if (owner == nullptr || attr == nullptr || attr->GetQueryColumn() == nullptr || owner->GetDataReference() == nullptr)
		return;
	const ibBackendQueryColumn* const refCol = owner->GetDataReference()->GetQueryColumn();
	if (refCol == nullptr || owner->GetQueryable() == nullptr)
		return;
	try {
		ibDataQueryBuilder query;
		query.From(owner->GetQueryable());
		query.Where(attr->GetQueryColumn(), value);
		ibReadPageRequest page;
		page.m_count = 0;
		ibDataQueryResult rows = query.Execute(page);
		while (rows.Next()) {
			const ibValue ref = rows.GetValue(refCol);
			if (!found->Contains(ref))
				found->Add(ref);
		}
	}
	catch (...) {}
}
ibValue ibValueFilterCriteriaManager::Find(const ibValue& value) const
{
	ibValueArray* const found = new ibValueArray();
	if (m_meta == nullptr || value.IsEmpty() || ibBackendException::IsEvalComplete())
		return found;
	wxString text = m_meta->GetContent();
	text.Replace(wxT(","), wxT("\n"));
	text.Replace(wxT(";"), wxT("\n"));
	while (!text.IsEmpty()) {
		const bool more = text.Contains(wxT('\n'));
		wxString line = text.BeforeFirst(wxT('\n'));
		text = more ? text.AfterFirst(wxT('\n')) : wxString();
		line.Trim(true).Trim(false);
		std::vector<wxString> parts;
		SplitPath(line, parts);
		if (parts.size() < 2)
			continue;
		const wxString attribute = parts.back();
		const wxString object = parts.size() == 2 ? parts[0] : parts[1];
		const ibClassID kind = parts.size() == 2 ? 0 : KindOf(parts[0]);
		ibMetaData* const meta = m_meta->GetMetaData();
		const ibClassID kinds[2] = { kind != 0 ? kind : g_metaDocumentCLSID, kind != 0 ? kind : g_metaCatalogCLSID };
		const int count = kind != 0 ? 1 : 2;
		for (int i = 0; i < count; ++i) {
			ibValueMetaObject* const owner = NamedOf(meta, kinds[i], object);
			ibValueMetaObjectRecordDataRef* const record = dynamic_cast<ibValueMetaObjectRecordDataRef*>(owner);
			SearchOne(record, AttributeNamed(owner, attribute), value, found);
		}
	}
	return found;
}
bool ibValueFilterCriteriaManager::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	if (lMethodNum != 0 || lSizeArray < 1 || paParams == nullptr || paParams[0] == nullptr)
		return false;
	pvarRetValue = Find(*paParams[0]);
	return true;
}
wxString ibValueFilterCriteriaManager::GetClassName() const
{
	if (m_meta != nullptr && m_meta->GetMetaData() != nullptr) {
		const ibCtorMetaValueType* const cls = m_meta->GetMetaData()->GetTypeCtor(
			m_meta, ibCtorObjectMetaType::ibCtorObjectMetaType_Manager);
		if (cls != nullptr)
			return cls->GetClassName();
	}
	return wxT("FilterCriteriaManager");
}
} // namespace
bool ibValueMetaObjectFilterCriteria::OnLoadMetaObject(ibMetaData* metaData)
{
	if (!ibValueMetaObject::OnLoadMetaObject(metaData))
		return false;
	if (metaData != nullptr && metaData->GetTypeCtor(this, ibCtorObjectMetaType::ibCtorObjectMetaType_Manager) == nullptr)
		metaData->RegisterCtor(new ibCtorFilterCriteriaManager(this));
	return true;
}
bool ibValueMetaObjectFilterCriteria::OnDeleteMetaObject()
{
	if (m_metaData != nullptr && m_metaData->GetTypeCtor(this, ibCtorObjectMetaType::ibCtorObjectMetaType_Manager) != nullptr)
		m_metaData->UnRegisterCtor(manager_to_clsid(GetMetaID(), clsid_metaclass(GetClassType())));
	return ibValueMetaObject::OnDeleteMetaObject();
}
bool ibValueMetaObjectFilterCriteria::ReadData(const ibDataNode& node)
{
	m_propertyContent->SetNodeValue(node.GetProperty(m_propertyContent->GetName()));
	return ibValueMetaObject::ReadData(node);
}
bool ibValueMetaObjectFilterCriteria::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyContent->GetName(), m_propertyContent->GetNodeValue());
	return ibValueMetaObject::WriteData(node);
}
METADATA_TYPE_REGISTER(ibValueMetaObjectFilterCriteria, "FilterCriteria", g_metaFilterCriteriaCLSID);
