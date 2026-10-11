#include "metaDocumentJournalObject.h"
#include "backend/metaData.h"
#include "backend/metaCtor.h"
#include "backend/objCtor.h"
#include "backend/system/value/valueArray.h"
namespace {
class ibValueDocumentJournalManager : public ibValueDynamicMembers {
public:
	explicit ibValueDocumentJournalManager(ibValueMetaObjectDocumentJournal* meta = nullptr)
		: ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, true), m_meta(meta)
	{
		m_members.Bind(this, &ibValueDocumentJournalManager::Fill);
	}
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray) override;
	virtual wxString GetClassName() const override;
private:
	void Fill(ibMemberTable& helper) const { helper.AppendFunc(wxT("Documents"), 0, wxT("Documents()")); }
	ibValue Documents() const;
	ibValueMetaObjectDocumentJournal* m_meta;
};
class ibCtorDocumentJournalManager : public ibCtorMetaValueType {
public:
	explicit ibCtorDocumentJournalManager(ibValueMetaObjectDocumentJournal* meta) : m_meta(meta) {
		m_classType = manager_to_clsid(meta->GetMetaID(), clsid_metaclass(meta->GetClassType()));
	}
	virtual wxString GetClassName() const override {
		return m_meta->GetClassName() + prefixManager + m_meta->GetName();
	}
	virtual ibClassID GetClassType() const override { return m_classType; }
	virtual ibValue CreateObject() const override { return new ibValueDocumentJournalManager(m_meta); }
	virtual const ibValueMetaObject* GetMetaObject() const override { return m_meta; }
	virtual ibCtorObjectMetaType GetMetaTypeCtor() const override { return ibCtorObjectMetaType::ibCtorObjectMetaType_Manager; }
private:
	ibClassID m_classType = 0;
	ibValueMetaObjectDocumentJournal* m_meta;
};
wxString DocumentName(wxString token)
{
	token.Trim(true).Trim(false);
	if (token.IsEmpty())
		return token;
	if (token.Contains(wxT('.')))
		token = token.AfterLast(wxT('.'));
	token.Trim(true).Trim(false);
	return token;
}
ibValue ibValueDocumentJournalManager::Documents() const
{
	ibValueArray* const names = new ibValueArray();
	if (m_meta == nullptr)
		return names;
	wxString text = m_meta->GetContent();
	text.Replace(wxT(","), wxT("\n"));
	text.Replace(wxT(";"), wxT("\n"));
	while (!text.IsEmpty()) {
		const bool more = text.Contains(wxT('\n'));
		wxString line = text.BeforeFirst(wxT('\n'));
		text = more ? text.AfterFirst(wxT('\n')) : wxString();
		const wxString name = DocumentName(line);
		if (!name.IsEmpty())
			names->Add(ibValue(name));
	}
	return names;
}
bool ibValueDocumentJournalManager::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	(void)paParams;
	(void)lSizeArray;
	if (lMethodNum != 0)
		return false;
	pvarRetValue = Documents();
	return true;
}
wxString ibValueDocumentJournalManager::GetClassName() const
{
	if (m_meta != nullptr && m_meta->GetMetaData() != nullptr) {
		const ibCtorMetaValueType* const cls = m_meta->GetMetaData()->GetTypeCtor(
			m_meta, ibCtorObjectMetaType::ibCtorObjectMetaType_Manager);
		if (cls != nullptr)
			return cls->GetClassName();
	}
	return wxT("DocumentJournalManager");
}
} // namespace
bool ibValueMetaObjectDocumentJournal::OnBeforeRunMetaObject(int flags)
{
	if (!ibValueMetaObject::OnBeforeRunMetaObject(flags))
		return false;
	if (m_metaData != nullptr && m_metaData->IsConfigOpen()
		&& m_metaData->GetTypeCtor(this, ibCtorObjectMetaType::ibCtorObjectMetaType_Manager) == nullptr)
		m_metaData->RegisterCtor(new ibCtorDocumentJournalManager(this));
	return true;
}
bool ibValueMetaObjectDocumentJournal::OnDeleteMetaObject()
{
	if (m_metaData != nullptr && m_metaData->GetTypeCtor(this, ibCtorObjectMetaType::ibCtorObjectMetaType_Manager) != nullptr)
		m_metaData->UnRegisterCtor(manager_to_clsid(GetMetaID(), clsid_metaclass(GetClassType())));
	return ibValueMetaObject::OnDeleteMetaObject();
}
bool ibValueMetaObjectDocumentJournal::ReadData(const ibDataNode& node)
{
	m_propertyContent->SetNodeValue(node.GetProperty(m_propertyContent->GetName()));
	return ibValueMetaObject::ReadData(node);
}
bool ibValueMetaObjectDocumentJournal::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyContent->GetName(), m_propertyContent->GetNodeValue());
	return ibValueMetaObject::WriteData(node);
}
METADATA_TYPE_REGISTER(ibValueMetaObjectDocumentJournal, "DocumentJournal", g_metaDocumentJournalCLSID);
