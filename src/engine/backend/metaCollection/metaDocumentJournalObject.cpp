#include "metaDocumentJournalObject.h"
#include "backend/metaData.h"
#include "backend/metaCtor.h"
#include "backend/objCtor.h"
#include "backend/appData.h"
#include "backend/backend_exception.h"
#include "backend/system/value/valueArray.h"
#include "backend/query/queryableFactory.h"
#include "backend/query/tempTableQueryable.h"
#include "backend/metaCollection/partial/document.h"
#include <vector>
namespace {
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
std::vector<wxString> DocumentNames(const ibValueMetaObjectDocumentJournal* meta)
{
	std::vector<wxString> names;
	if (meta == nullptr)
		return names;
	wxString text = meta->GetContent();
	text.Replace(wxT(","), wxT("\n"));
	text.Replace(wxT(";"), wxT("\n"));
	while (!text.IsEmpty()) {
		const bool more = text.Contains(wxT('\n'));
		wxString line = text.BeforeFirst(wxT('\n'));
		text = more ? text.AfterFirst(wxT('\n')) : wxString();
		const wxString name = DocumentName(line);
		if (!name.IsEmpty())
			names.push_back(name);
	}
	return names;
}
ibValueMetaObjectDocument* FindDocument(const ibValueMetaObjectDocumentJournal* meta, const wxString& name)
{
	if (meta == nullptr || meta->GetMetaData() == nullptr)
		return nullptr;
	const ibValueMetaObject* const root = meta->GetMetaData()->GetCommonMetaObject();
	if (root == nullptr)
		return nullptr;
	return root->FindAnyObjectByFilter<ibValueMetaObjectDocument>(name);
}
} // namespace
static ibMetaID JournalColumn(int which)
{
	return ibBackendQueryColumn::SyntheticId(ibBackendQueryColumn::SyntheticKind::Derived, which);
}
class ibDocumentJournalQueryable : public ibBackendQueryable {
public:
	explicit ibDocumentJournalQueryable(const ibValueMetaObjectDocumentJournal* meta) : m_meta(meta) {}
	const ibBackendQueryColumn* ResolveColumnByName(const wxString& name) const override;
	std::vector<const ibBackendQueryColumn*> GetColumns() const override;
	std::vector<const ibBackendQueryColumn*> GetPrimaryKeyColumns() const override;
	wxString GetQueryTableName() const override { return wxEmptyString; }
	wxString GetQueryName() const override { return m_meta != nullptr ? m_meta->GetName() : wxString(); }
	ibMetaID GetQueryTableId() const override { return m_meta != nullptr ? m_meta->GetMetaID() : 0; }
	const ibMetaData* GetMetaData() const override { return m_meta != nullptr ? m_meta->GetMetaData() : nullptr; }
	bool IsComputedInRam() const override { return true; }
	ibBackendQueryProvider& GetProvider() const override { return ibComputedProviderInstance(); }
	ibQueryRamTable ComputeRows(const std::vector<ibQueryCondition>& extra) const override;
private:
	void EnsureColumns() const;
	const ibValueMetaObjectDocumentJournal* m_meta;
	mutable std::vector<std::unique_ptr<ibTempColumn>> m_columns;
};
void ibDocumentJournalQueryable::EnsureColumns() const
{
	if (!m_columns.empty() || m_meta == nullptr)
		return;
	ibTypeDescription refType;
	ibTypeDescription dateType;
	ibTypeDescription numberType;
	for (const wxString& name : DocumentNames(m_meta)) {
		const ibValueMetaObjectDocument* const doc = FindDocument(m_meta, name);
		if (doc == nullptr)
			continue;
		if (const ibValueMetaObjectAttributePredefined* ref = doc->GetDataReference())
			refType.AppendMetaType(ref->GetTypeDesc().GetClsidList(), ref->GetTypeDesc().GetTypeData());
		if (!dateType.IsOk())
			if (const ibValueMetaObjectAttributePredefined* date = doc->GetDocumentDate())
				dateType.SetDefaultMetaType(date->GetTypeDesc());
		if (!numberType.IsOk())
			if (const ibValueMetaObjectAttributePredefined* number = doc->GetDocumentNumber())
				numberType.SetDefaultMetaType(number->GetTypeDesc());
	}
	m_columns.push_back(std::make_unique<ibTempColumn>(wxT("Ref"), refType, JournalColumn(1)));
	m_columns.push_back(std::make_unique<ibTempColumn>(wxT("Date"), dateType, JournalColumn(2)));
	m_columns.push_back(std::make_unique<ibTempColumn>(wxT("Number"), numberType, JournalColumn(3)));
}
const ibBackendQueryColumn* ibDocumentJournalQueryable::ResolveColumnByName(const wxString& name) const
{
	EnsureColumns();
	for (const auto& column : m_columns)
		if (column->GetName().CmpNoCase(name) == 0)
			return column.get();
	return nullptr;
}
std::vector<const ibBackendQueryColumn*> ibDocumentJournalQueryable::GetColumns() const
{
	EnsureColumns();
	std::vector<const ibBackendQueryColumn*> columns;
	columns.reserve(m_columns.size());
	for (const auto& column : m_columns)
		columns.push_back(column.get());
	return columns;
}
std::vector<const ibBackendQueryColumn*> ibDocumentJournalQueryable::GetPrimaryKeyColumns() const
{
	EnsureColumns();
	return m_columns.empty() ? std::vector<const ibBackendQueryColumn*>{} : std::vector<const ibBackendQueryColumn*>{ m_columns.front().get() };
}
ibQueryRamTable ibDocumentJournalQueryable::ComputeRows(const std::vector<ibQueryCondition>&) const
{
	EnsureColumns();
	ibQueryRamTable table;
	for (const auto& column : m_columns)
		table.AddColumn(column->GetColumnId(), column->GetName(), column->GetTypeDesc());
	if (m_meta == nullptr || ibApplicationInstance::Get(false) == nullptr)
		return table;
	if (ibApplicationInstance::GetDatabaseLayer() == nullptr)
		return table;
	for (const wxString& name : DocumentNames(m_meta)) {
		const ibValueMetaObjectDocument* const doc = FindDocument(m_meta, name);
		if (doc == nullptr || doc->GetQueryable() == nullptr)
			continue;
		const ibBackendQueryColumn* const ref = doc->GetDataReference() != nullptr ? doc->GetDataReference()->GetQueryColumn() : nullptr;
		const ibBackendQueryColumn* const date = doc->GetDocumentDate() != nullptr ? doc->GetDocumentDate()->GetQueryColumn() : nullptr;
		const ibBackendQueryColumn* const number = doc->GetDocumentNumber() != nullptr ? doc->GetDocumentNumber()->GetQueryColumn() : nullptr;
		if (ref == nullptr || date == nullptr || number == nullptr)
			continue;
		try {
			ibDataQueryBuilder door;
			door.From(doc->GetQueryable());
			door.Select(ref, wxT("Ref"));
			door.Select(date, wxT("Date"));
			door.Select(number, wxT("Number"));
			ibReadPageRequest page;
			ibDataQueryResult result = door.Execute(page);
			while (result.Next()) {
				const long row = table.AppendRow();
				table.SetCell(row, JournalColumn(1), result.GetValue(ref));
				table.SetCell(row, JournalColumn(2), result.GetValue(date));
				table.SetCell(row, JournalColumn(3), result.GetValue(number));
			}
		}
		catch (const ibBackendException&) {
			// This document has no table yet. The journal still answers for the others.
		}
	}
	return table;
}
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
	void Fill(ibMemberTable& helper) const {
		helper.AppendFunc(wxT("Documents"), 0, wxT("Documents()"));
		helper.AppendFunc(wxT("Select"), 0, wxT("Select()"));
	}
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
ibValue ibValueDocumentJournalManager::Documents() const
{
	ibValueArray* const names = new ibValueArray();
	for (const wxString& name : DocumentNames(m_meta))
		names->Add(ibValue(name));
	return names;
}
bool ibValueDocumentJournalManager::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	(void)paParams;
	(void)lSizeArray;
	if (m_meta == nullptr)
		return false;
	if (lMethodNum == 0) {
		pvarRetValue = Documents();
		return true;
	}
	if (lMethodNum == 1) {
		pvarRetValue = m_meta->Select();
		return true;
	}
	return false;
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
class ibDocumentJournalSource : public ibQueryableSourceDescriptor {
public:
	explicit ibDocumentJournalSource(ibValueMetaObjectDocumentJournal* meta) : m_meta(meta), m_rows(meta) {}
	wxString GetNamespace() const override { return ibValue::GetNameObjectFromID(m_meta->GetClassType()); }
	wxString GetName() const override { return m_listed.IsEmpty() ? m_meta->GetName() : m_listed; }
	const ibBackendQueryable* CreateQueryable(ibValue**, long) override { return &m_rows; }
	void ListAs(const wxString& name) { m_listed = name; }
	ibDocumentJournalQueryable& Rows() { return m_rows; }
private:
	ibValueMetaObjectDocumentJournal* m_meta;
	ibDocumentJournalQueryable m_rows;
	wxString m_listed;
};
ibValueMetaObjectDocumentJournal::ibValueMetaObjectDocumentJournal()
	: ibValueMetaObject()
{
}
ibValueMetaObjectDocumentJournal::~ibValueMetaObjectDocumentJournal() = default;
ibValue ibValueMetaObjectDocumentJournal::Select() const
{
	ibValueArray* const refs = new ibValueArray();
	if (m_source == nullptr)
		return refs;
	const ibQueryRamTable rows = m_source->Rows().ComputeRows({});
	for (long i = 0; i < rows.RowCount(); ++i)
		refs->Add(rows.GetCell(i, JournalColumn(1)));
	return refs;
}
void ibValueMetaObjectDocumentJournal::Reindex(const wxString& name)
{
	if (m_source == nullptr || m_metaData == nullptr)
		return;
	m_metaData->UnregisterSource(m_source.get());
	m_source->ListAs(name);
	m_metaData->RegisterSource(m_source.get());
	m_source->ListAs(wxEmptyString);
}
bool ibValueMetaObjectDocumentJournal::OnBeforeRunMetaObject(int flags)
{
	if (!ibValueMetaObject::OnBeforeRunMetaObject(flags))
		return false;
	if (m_metaData != nullptr && m_metaData->IsConfigOpen()
		&& m_metaData->GetTypeCtor(this, ibCtorObjectMetaType::ibCtorObjectMetaType_Manager) == nullptr)
		m_metaData->RegisterCtor(new ibCtorDocumentJournalManager(this));
	return true;
}
bool ibValueMetaObjectDocumentJournal::OnAfterRunMetaObject(int flags)
{
	if (!ibValueMetaObject::OnAfterRunMetaObject(flags))
		return false;
	if (m_metaData != nullptr && m_metaData->GetSourceFactory() != nullptr) {
		m_source = std::make_unique<ibDocumentJournalSource>(this);
		m_metaData->RegisterSource(m_source.get());
	}
	return true;
}
bool ibValueMetaObjectDocumentJournal::OnBeforeCloseMetaObject()
{
	if (m_source != nullptr && m_metaData != nullptr)
		m_metaData->UnregisterSource(m_source.get());
	m_source.reset();
	return ibValueMetaObject::OnBeforeCloseMetaObject();
}
bool ibValueMetaObjectDocumentJournal::OnRenameMetaObject(const wxString& newName)
{
	Reindex(newName);
	return ibValueMetaObject::OnRenameMetaObject(newName);
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
