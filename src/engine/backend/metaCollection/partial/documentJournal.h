#ifndef __DOCUMENT_JOURNAL_H__
#define __DOCUMENT_JOURNAL_H__

// A DOCUMENT JOURNAL — one list over the documents of several kinds.
//
// It keeps nothing of its own: no table, no rows, no object. What it declares is WHICH documents it
// lists (`RegisteredDocuments`, a binding like a document's register records), and what it is read
// as is those documents' own rows, stacked: `DocumentJournal.<Name>` in a query, the list form, and
// `DocumentJournals.<Name>.Select()`. Every one of them reads the same source.
//
// ⭐ THE ROWS STAY ON THE SERVER. The source is a derived table - one `SELECT` per document,
// `UNION ALL` between them (GetSourceRelation) - so a list pages, sorts and filters it as it would a
// table, and a document added to the journal is one more branch rather than one more read.

#include "commonObject.h"
#include "backend/query/tempTableQueryable.h"                       // ibTempColumn — a column the journal publishes
#include "backend/propertyManager/property/propertyGeneration.h"   // the binding the documents are listed by

#include <memory>

class ibValueMetaObjectDocument;
class ibValueMetaObjectDocumentJournal;

// The documents a journal lists - the documents of the configuration, and only them.
class BACKEND_API ibPropertyJournalDocuments : public ibPropertyGeneration {
public:
	using ibPropertyGeneration::ibPropertyGeneration;
	virtual ibPropertyChoiceMode GetValueList(ibPropertyChoiceList& list) override;
};

// ==========================================================================
// The journal's rows: one per written document of every kind it lists.
//
// The columns are those every document has - its reference, date, number, posting flag and deletion
// mark. Each one's type is the union of the documents' own (a reference to any of them, the number as
// each document keeps it), and its fields are spread the way any column of that type is, so a row is
// read by the ordinary codec: the `_TYPE` tag says which field holds the value.
// ==========================================================================
class BACKEND_API ibDocumentJournalQueryable : public ibBackendQueryable {
public:
	explicit ibDocumentJournalQueryable(const ibValueMetaObjectDocumentJournal* meta);

	const ibBackendQueryColumn* GetRefColumn() const { Retype(); return m_ref.get(); }
	const ibBackendQueryColumn* GetDateColumn() const { Retype(); return m_date.get(); }

	virtual const ibBackendQueryColumn* ResolveColumnByName(const ibString& name) const override;
	virtual std::vector<const ibBackendQueryColumn*> GetColumns() const override;
	virtual std::vector<const ibBackendQueryColumn*> GetPrimaryKeyColumns() const override { return { GetRefColumn() }; }

	// The name the derived table is known by inside a statement - never a table that exists.
	virtual ibString GetQueryTableName() const override;
	virtual ibString GetQueryName() const override;
	virtual const ibMetaData* GetMetaData() const override;
	virtual const ibValueMetaObjectGenericData* GetSourceMetaObject() const override;

	// One SELECT per listed document, UNION ALL between them. Null when no document is listed: a journal
	// of nothing has no rows to read, and the caller is told so rather than handed an empty union.
	virtual ibQueryRelPtr GetSourceRelation(const ibString& alias) const override;

	// …and is answered in memory: no rows, the columns it would have.
	virtual bool IsComputedInRam() const override;
	virtual ibBackendQueryProvider& GetProvider() const override;
	virtual ibQueryRamTable ComputeRows(const std::vector<ibQueryCondition>& extra) const override;

	virtual const ibBackendQueryColumn* GetSortColumn() const override { return GetDateColumn(); }

private:
	// The columns, their types brought up to date with the documents listed now: the list changes in the
	// designer while the queryable lives, and the columns are handed out by pointer, so they are kept and
	// retyped rather than made again.
	void Retype() const;

	const ibValueMetaObjectDocumentJournal* m_meta;
	std::unique_ptr<ibTempColumn> m_ref, m_date, m_number, m_posted, m_deletionMark;
};

// What the factory knows the journal by - `DocumentJournal.<Name>` - and the row surface a list asks: the
// value a row picks, its identity, and opening it. A row is a document, so opening it opens the document.
class BACKEND_API ibDocumentJournalSource : public ibQueryableSourceDescriptor {
public:
	explicit ibDocumentJournalSource(const ibValueMetaObjectDocumentJournal* meta) : m_meta(meta), m_rows(meta) {}

	ibString GetNamespace() const override;
	ibString GetName() const override;
	const ibBackendQueryable* CreateQueryable(ibValue** /*paParams*/, long /*lSizeArray*/) override { return &m_rows; }
	const ibDocumentJournalQueryable* GetQueryable() const { return &m_rows; }

	void FillSourceExplorer(ibSourceDataObject::ibSourceExplorer& explorer) const override;

	ibValue GetSelectValue(const ibRowMetaValues& rowValues) const override;
	ibUniqueKey GetItemKey(const ibRowMetaValues& rowValues) const override;
	void ShowValueByKey(const ibUniqueKey& key, ibBackendValueForm* srcForm) const override;

private:
	const ibValueMetaObjectDocumentJournal* m_meta;
	ibDocumentJournalQueryable m_rows;
};

class BACKEND_API ibValueMetaObjectDocumentJournal : public ibValueMetaObjectGenericData {
public:

	// A manager and nothing else: no reference (a row is a document's), no object, no record set.
	static constexpr unsigned s_features = ibMetaFeature_Manager;

	enum
	{
		eFormList = 1,
	};

	virtual ibFormTypeList GetFormType() const override {
		ibFormTypeList formList;
		formList.AppendItem(ibT("FormList"), _("Form list"), eFormList);
		return formList;
	}

	ibValueMetaObjectDocumentJournal();
	virtual ~ibValueMetaObjectDocumentJournal();

	// A journal is something a person opens, so it may stand in a section.
	virtual bool IsInterfaceAllowed() const override { return true; }

	// The documents listed, in the order they were bound. A document deleted since is not among them.
	std::vector<const ibValueMetaObjectDocument*> GetDocuments() const;
	ibMetaDescription& GetRegisteredDocuments() const { return m_propertyDocuments->GetValueAsMetaDesc(); }

	const ibDocumentJournalQueryable* GetQueryable() const { return m_source.GetQueryable(); }

	// The references of the documents listed, by date, between two moments (an empty one bounds nothing).
	ibValue Select(const ibValue& from = ibValue(), const ibValue& to = ibValue()) const;

	virtual bool OnBeforeRunMetaObject(int flags) override;
	virtual bool OnAfterRunMetaObject(int flags) override;
	virtual bool OnBeforeCloseMetaObject() override;
	virtual bool OnAfterCloseMetaObject() override;

	virtual void OnCreateFormObject(ibValueMetaObjectFormBase* metaForm) override;
	virtual void OnRemoveMetaForm(ibValueMetaObjectFormBase* metaForm) override;
	virtual ibValueMetaObjectFormBase* GetDefaultFormByID(const ibFormID& id) const override;

	ibFormPtr<ibBackendValueForm> GetListForm(const ibFormRequest& request = ibFormRequest(),
		ibBackendControlFrame* ownerControl = nullptr) const;

	virtual std::vector<ibValueMetaObjectAttributeBase*> GetGenericAttributeArrayObject(
		std::vector<ibValueMetaObjectAttributeBase*>& array) const override { return array; }

protected:

	virtual ibFormPtr<ibBackendValueForm> GetFormByCommandType(ibInterfaceCommandType /*cmdType*/ = ibInterfaceCommandType::ibInterfaceCommandType_Default) const override {
		return GetListForm();
	}

	virtual ibValuePtr<ibValueManagerDataObject> CreateManagerDataObjectValue() const override;
	virtual ibSourcePtr<ibSourceDataObject> CreateSourceObject(const ibCreateRequest& request, const ibFormID& form_id) const override;

	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

private:

	bool FillFormList(ibPropertyList* prop) {
		for (auto object : GetFormArrayObject()) {
			if (!object->IsAllowed()) continue;
			if (eFormList == object->GetTypeForm())
				prop->AppendItem(object->GetName(), object->GetMetaID(), object->GetClassType(), object);
		}
		return true;
	}

	ibPropertyCategory* m_categoryDocuments = ibPropertyObject::CreatePropertyCategory(ibT("Documents"), _("Documents"));
	ibPropertyJournalDocuments* m_propertyDocuments = ibPropertyObject::CreateProperty<ibPropertyJournalDocuments>(m_categoryDocuments, ibT("RegisteredDocuments"), _("Registered documents"), _("The documents this journal lists. Its rows are theirs: one per written document of each kind listed, with the reference, date, number, posting flag and deletion mark every document has."));

	ibPropertyCategory* m_categoryForm = ibPropertyObject::CreatePropertyCategory(ibT("PresetValues"), _("Preset values"));
	ibPropertyList* m_propertyDefFormList = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryForm, ibT("DefaultFormList"), _("Default List Form"), _("The form the journal's list opens with. Empty: the list form is generated from the journal's columns."), &ibValueMetaObjectDocumentJournal::FillFormList);

	ibDocumentJournalSource m_source{ this };
};

#endif
