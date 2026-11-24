#ifndef __DOCUMENT_JOURNAL_H__
#define __DOCUMENT_JOURNAL_H__

// A DOCUMENT JOURNAL — one list over the documents of several kinds.
//
// It keeps nothing of its own: no table, no rows, no object. What it declares is WHICH documents it
// lists (`RegisteredDocuments`, a binding like a document's register records), and what it is read
// as is those documents' own rows, stacked: `DocumentJournal.<Name>` in a query, the list form, and the
// selection `DocumentJournals.<Name>.Select()`. Every one of them reads the queryable the journal holds.
//
// ⭐ THE ROWS STAY ON THE SERVER. The source is a derived table - one `SELECT` per document,
// `UNION ALL` between them (GetSourceRelation) - so a list pages, sorts and filters it as it would a
// table, and a document added to the journal is one more branch rather than one more read.
//
// ⭐⭐ …AND A BRANCH IS READ AS ITS DOCUMENT IS. The queryable sets the access up itself: each branch is the
// document's own read under the session's policy - the document's right asked, its roles' row filter folded
// in - so the journal shows what reading each document would show. The policy then asks the journal for its
// own right and its own filter, as it asks any source a query names.

#include "commonObject.h"
#include "backend/srcColumn.h"                                     // ibBackendSourceColumn — a journal column in a list
#include "backend/query/tempColumn.h"                              // ibTempColumn — a column the journal publishes
#include "backend/propertyManager/property/propertyGeneration.h"   // the binding the documents are listed by

#include <memory>
#include <vector>

class ibValueMetaObjectDocument;
class ibValueMetaObjectDocumentJournal;

// The documents a journal lists - the documents of the application, and only them.
class BACKEND_API ibPropertyJournalDocuments : public ibPropertyGeneration {
public:
	using ibPropertyGeneration::ibPropertyGeneration;
	virtual ibPropertyChoiceMode GetValueList(ibPropertyChoiceList& list) override;
};

// ==========================================================================
// The journal's rows: one per written document of every kind it lists.
//
// The columns are those every document has - its reference, date, number, posting flag and deletion
// mark - and one none has: the document's type, which document the row is. Each one's type is the union of
// the documents' own (a reference to any of them, the number as each document keeps it), and its fields are
// spread the way any column of that type is, so a row is read by the ordinary codec: the `_TYPE` tag says
// which field holds the value.
//
// Typed by the documents listed when it is made: the journal makes another when it runs and when the list
// changes, rather than retyping columns somebody else already holds.
// ==========================================================================
class BACKEND_API ibDocumentJournalQueryable : public ibBackendQueryable {
public:
	explicit ibDocumentJournalQueryable(const ibValueMetaObjectDocumentJournal* meta);

	virtual std::vector<std::shared_ptr<const ibBackendQueryColumn>> GetQueryColumns() const override;
	virtual std::vector<std::shared_ptr<const ibBackendQueryColumn>> GetPrimaryKeyColumns() const override { return { m_ref }; }

	// The name the derived table is known by inside a statement - never a table that exists.
	virtual ibString GetQueryTableName() const override;
	virtual const ibUniqueKey& GetQueryTableGuid() const override;
	virtual ibString GetQueryName() const override;
	virtual ibMetaID GetQueryTableId() const override;
	virtual const ibMetaData* GetMetaData() const override;
	virtual const ibValueMetaObjectGenericData* GetSourceMetaObject() const override;

	// One SELECT per listed document, UNION ALL between them, each read under the session's policy. Null when
	// no document is listed: a journal of nothing has no rows to read…
	virtual ibQueryRelPtr GetSourceRelation(const ibString& alias) const override;

	// …and is answered in memory: no rows, the columns it would have.
	virtual ibBackendQueryProvider& GetProvider() const override;
	virtual ibQueryRamTable ComputeRows(const std::vector<ibQueryCondition>& extra) const override;

	virtual std::shared_ptr<const ibBackendQueryColumn> GetSortColumn() const override { return m_date; }

	const std::shared_ptr<const ibBackendQueryColumn>& GetRefColumn() const { return m_ref; }
	const std::shared_ptr<const ibBackendQueryColumn>& GetDateColumn() const { return m_date; }
	// Which document the row is - the one column no document has: the type of its reference, a Type.
	const std::shared_ptr<const ibBackendQueryColumn>& GetDocumentTypeColumn() const { return m_documentType; }

private:
	// ⭐ WHICH DOCUMENT THE ROW IS, AS A TYPE - its reference's, `Type("DocumentRef.Invoice")`. Nothing stores it:
	// the reference's own type field holds it, so this column is MADE of that field, as a document's moment is made
	// of its date and its reference (Kind::Synthetic). A sort by it is a sort by that field, a comparison binds a
	// type's class there, and a read makes the type out of what is there.
	class ibTypeColumn : public ibBackendQueryColumn {
	public:
		ibTypeColumn(const ibValueMetaObjectDocumentJournal* meta, std::shared_ptr<const ibBackendQueryColumn> ref, ibMetaID id);

		ibString GetName() const override;
		ibString GetSynonym() const override;
		ibString GetPhysicalName() const override;
		ibTypeDescription& GetTypeDesc() const override { return m_type; }
		ibMetaID GetColumnId() const override { return m_id; }

		Kind GetColumnKind() const override { return Kind::Synthetic; }
		std::vector<ibColumnSlot> DescribeLayout() const override;
		bool ReadValue(const ibString& fieldName, const class ibMetaData* metaData,
		               class ibValue& retValue, class ibQueryResult& result, bool createData = false) const override;
		void BindValue(class ibQueryStatement& statement, const class ibMetaData* metaData,
		               const class ibValue& value, int& position) const override;

		const class ibMetaData* GetMetaData() const override;

	private:
		const ibValueMetaObjectDocumentJournal*     m_meta;
		std::shared_ptr<const ibBackendQueryColumn> m_ref;
		ibMetaID                                    m_id;
		mutable ibTypeDescription                   m_type;   // the interface hands out a non-const reference
	};

	const ibValueMetaObjectDocumentJournal*       m_meta;
	std::vector<const ibValueMetaObjectDocument*> m_documents;   // what the columns' types were joined over
	std::shared_ptr<const ibBackendQueryColumn>   m_ref, m_date, m_number, m_posted, m_deletionMark, m_documentType;
};

// What the factory knows the journal by - `DocumentJournal.<Name>` - and the row surface a list asks: the
// value a row picks, its identity, its picture, the commands and opening it. A row is a document, so each of
// them is the document's: its list's commands, its row picture, its form.
class BACKEND_API ibDocumentJournalSource : public ibQueryableSourceDescriptor {
public:
	explicit ibDocumentJournalSource(const ibValueMetaObjectDocumentJournal* meta);

	ibString GetNamespace() const override;
	ibString GetName() const override;
	// No arguments: the journal's own reading, shared without owning.
	std::shared_ptr<const ibBackendQueryable> CreateQueryable(ibValue** paParams, long lSizeArray,
	                                                          const std::vector<ibQueryPredicatePtr>& conditions,
	                                                          const ibQueryReadColumns& read) override;
	std::vector<std::shared_ptr<const ibBackendQueryColumn>> GetQueryColumns(const std::vector<ibValue>& args) const override;

	void FillSourceExplorer(ibSourceDataObject::ibSourceExplorer& explorer) const override;

	ibValue GetSelectValue(const ibRowMetaValues& rowValues) const override;
	ibUniqueKey GetItemKey(const ibRowMetaValues& rowValues) const override;
	// The row's document's picture: posted, marked for deletion or neither, as its own list draws it.
	ibPictureID GetRowPicture(const ibRowMetaValues& rowValues) const override;

	// The commands of a document's list. A row's is run by the row's document; Add asks which document, when
	// the journal lists more than one.
	void GetCommandCollection(const ibFormID& formType, std::vector<ibCommandItem>& commands) const override;
	void CallAsCommand(ibActionID id, const ibUniqueKey& anchor, const ibUniqueKey& key, ibBackendValueForm* srcForm) const override;
	void ShowValueByKey(const ibUniqueKey& key, ibBackendValueForm* srcForm) const override;

private:
	// A column of the journal as a list binds to it: a column of the source with no attribute behind it, named,
	// captioned and typed as the journal's reading publishes it - asked of the reading each time, because the
	// journal makes another when its documents change. Lives as long as the journal, as a document's attributes
	// live as long as the document.
	class ibColumn : public ibBackendSourceColumn {
	public:
		ibColumn(const ibValueMetaObjectDocumentJournal* meta, ibMetaID id) : m_meta(meta), m_id(id) {}
		ibString GetName() const override;
		ibString GetSynonym() const override;
		ibTypeDescription& GetTypeDesc() const override;
		ibMetaID GetColumnId() const { return m_id; }
	private:
		const ibValueMetaObjectDocumentJournal* m_meta;
		ibMetaID                                m_id;
	};

	const ibValueMetaObjectDocumentJournal* m_meta;
	std::vector<ibColumn>                   m_columns;   // in the order a row lists them
};

class BACKEND_API ibValueMetaObjectDocumentJournal : public ibValueMetaObjectGenericData, public ibBackendQueryableHolder {
public:

	// A manager and nothing else: no reference (a row is a document's), no object, no record set.
	static constexpr unsigned s_features = ibMetaFeature_Manager;

	enum
	{
		eFormList = 1,
	};

	ibValueMetaObjectDocumentJournal();
	virtual ~ibValueMetaObjectDocumentJournal();

	virtual ibFormTypeList GetFormType() const override {
		ibFormTypeList formList;
		formList.AppendItem(ibT("FormList"), _("Form list"), eFormList);
		return formList;
	}

	virtual ibPicture GetIcon() const override;

	// A journal is something a person opens, so it may stand in a section…
	virtual bool IsInterfaceAllowed() const override { return true; }
	// …by whom its role lets read it — a right of its own, as every source a person opens has. What each row is,
	// its document's own right decides further in (the documents' reads).
	virtual bool AccessRight_Show() const override { return AccessRight_Read(); }

	// The journal's reading - what a query, the list and a selection read alike.
	virtual std::shared_ptr<const ibBackendQueryable> GetQueryable() const override { return m_queryable; }

	// A journal of no document is not saved: it would have no rows, no column types and nothing to add.
	virtual bool OnSaveMetaObject(int flags) override;

	// Its reading made again - another list of documents, or one of them changed or deleted.
	virtual bool OnReloadMetaObject() override;

	virtual bool OnBeforeRunMetaObject(int flags) override;
	virtual bool OnAfterRunMetaObject(int flags) override;
	virtual bool OnBeforeCloseMetaObject() override;
	virtual bool OnAfterCloseMetaObject() override;

	virtual void OnPropertyChanged(ibProperty* property, const ibVariant& oldValue, const ibVariant& newValue) override;

	virtual void OnCreateFormObject(ibValueMetaObjectFormBase* metaForm) override;
	virtual void OnRemoveMetaForm(ibValueMetaObjectFormBase* metaForm) override;
	virtual ibValueMetaObjectFormBase* GetDefaultFormByID(const ibFormID& id) const override;

	virtual std::vector<ibValueMetaObjectAttributeBase*> GetGenericAttributeArrayObject(
		std::vector<ibValueMetaObjectAttributeBase*>& array) const override { return array; }

	// The documents listed, in the order they were bound. A document deleted since is not among them.
	std::vector<const ibValueMetaObjectDocument*> GetDocuments() const;
	ibMetaDescription& GetRegisteredDocuments() const { return m_propertyDocuments->GetValueAsMetaDesc(); }

	ibFormPtr<ibBackendValueForm> GetListForm(const ibFormRequest& request = ibFormRequest(),
		ibBackendControlFrame* ownerControl = nullptr) const;

	bool AccessRight_Read() const { return IsFullAccess() || AccessRight(m_roleRead); }

	// What the kind is drawn with — the type registry asks the class, before any journal exists.
	static ibPicture GetIconGroup();

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

	// Made in the constructor, again when the journal runs and when its documents change (GetQueryable).
	std::shared_ptr<ibDocumentJournalQueryable> m_queryable;
	ibDocumentJournalSource m_source{ this };

	ibRole* m_roleRead = ibValueMetaObject::CreateRole(ibT("Read"), _("Read"));
};

#endif
