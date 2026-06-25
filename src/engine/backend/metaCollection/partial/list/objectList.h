#ifndef _OBJECT_LIST_H__
#define _OBJECT_LIST_H__

#include "backend/metaCollection/partial/commonObject.h"
#include "backend/metaCollection/partial/reference/reference.h"
#include "backend/composition/dataComposer.h"

#include <memory>

//base list class
class BACKEND_API ibValueListDataObject : public ibValueModelTableBase,
	public ibSourceDataObject {
	public:
protected:
	enum Func {
		enRefresh
	};
private:

	// implementation of base class virtuals to define model
	virtual ibValueModelColumnCollection* GetColumnCollection() const override { return m_recordColumnCollection; }
	virtual ibValueModelReturnLine* GetRowAt(const ibDataViewItem& line) override {
		if (!line.IsOk())
			return nullptr;
		return new ibValueDataObjectListReturnLine(this, line);
	}

public:

	virtual int Compare(const ibDataViewItem& item1, const ibDataViewItem& item2,
		unsigned int col, bool ascending) const override {
		wxASSERT(item1.IsOk() && item2.IsOk());
		const int long row1 = GetRow(item1);
		const int long row2 = GetRow(item2);
		if (row1 < row2)
			return -1;
		else if (row1 > row2)
			return 1;
		return 0;
	}

	class ibValueDataObjectListColumnCollection : public ibValueModelTableBase::ibValueModelColumnCollection {
	public:
		class ibValueDataObjectListColumnInfo : public ibValueModelTableBase::ibValueModelColumnCollection::ibValueModelColumnInfo {
	public:

			virtual unsigned int GetColumnID() const { return m_metaAttribute->GetMetaID(); }
			virtual wxString GetColumnName() const { return m_metaAttribute->GetName(); }
			virtual wxString GetColumnCaption() const { return m_metaAttribute->GetSynonym(); }
			virtual const ibTypeDescription GetColumnType() const { return m_metaAttribute->GetTypeDesc(); }

			ibValueDataObjectListColumnInfo();
			ibValueDataObjectListColumnInfo(ibValueMetaObjectAttributeBase* attribute);
			virtual ~ibValueDataObjectListColumnInfo();

		private:
			ibValueMetaObjectAttributeBase* m_metaAttribute;
		};

	public:

		ibValueDataObjectListColumnCollection();
		ibValueDataObjectListColumnCollection(ibValueListDataObject* ownerTable, const ibValueMetaObjectGenericData* metaObject);
		virtual ~ibValueDataObjectListColumnCollection();

		virtual const ibTypeDescription GetColumnType(unsigned int col) const {
			ibValueDataObjectListColumnInfo* columnInfo = m_listColumnInfo.at(col);
			wxASSERT(columnInfo);
			return columnInfo->GetColumnType();
		}

		virtual ibValueModelColumnInfo* GetColumnInfo(unsigned int idx) const {
			if (m_listColumnInfo.size() < idx)
				return nullptr;
			auto it = m_listColumnInfo.begin();
			std::advance(it, idx);
			return it->second;
		}

		virtual unsigned int GetColumnCount() const {
			const ibValueMetaObjectGenericData* metaTable = m_ownerTable->GetMetaObject();
			wxASSERT(metaTable);
			const auto& obj = metaTable->GetGenericAttributeArrayObject();
			return obj.size();
		}

		//array support 
		virtual bool SetAt(const ibValue& varKeyValue, const ibValue& varValue);
		virtual bool GetAt(const ibValue& varKeyValue, ibValue& pvarValue);

	protected:

		ibValueListDataObject* m_ownerTable;
		std::map<ibMetaID, ibValuePtr<ibValueDataObjectListColumnInfo>> m_listColumnInfo;
	};

	class ibValueDataObjectListReturnLine : public ibValueModelReturnLine {
	public:

		ibValueDataObjectListReturnLine(ibValueListDataObject* ownerTable = nullptr, const ibDataViewItem& line = ibDataViewItem(nullptr));
		virtual ~ibValueDataObjectListReturnLine();

		virtual ibValueModelTableBase* GetOwnerModel() const {
			return m_ownerTable;
		}


		void FillMembers(ibMemberTable& helper) const;

		virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal); //setting attribute
		virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal); //attribute value

	protected:
		ibValueListDataObject* m_ownerTable;
	};

public:

	void AppendSort(ibValueMetaObject* metaObject, bool ascending = true, bool use = true, bool system = false) {
		if (metaObject == nullptr)
			return;
		m_sortOrder.AppendSort(
			metaObject->GetMetaID(),
			metaObject->GetName(),
			metaObject->GetSynonym(),
			ascending, use, system
		);
	}

	virtual bool AutoCreateColumn() const { return false; }
	virtual bool EditableLine(const ibDataViewItem& item, unsigned int col) const { return false; }

	//set meta/get meta
	virtual bool SetValueByMetaID(const ibDataViewItem& item, const ibMetaID& id, const ibValue& varMetaVal) { return false; }
	virtual bool GetValueByMetaID(const ibDataViewItem& item, const ibMetaID& id, ibValue& pvarMetaVal) const {
		ibValueTableRow* node = GetViewData<ibValueTableRow>(item);
		if (node == nullptr)
			return false;
		return node->GetValue(id, pvarMetaVal);
	}

	//ctor
	ibValueListDataObject(const ibValueMetaObjectGenericData* metaObject = nullptr, const ibFormID& formType = wxNOT_FOUND, bool choiceMode = false);
	virtual ~ibValueListDataObject();

	//****************************************************************************
	//*                               Support model                              *
	//****************************************************************************

	//get metaData from object 
	virtual const ibValueMetaObjectGenericData* GetSourceMetaObject() const final { return GetMetaObject(); }
	// Metadata via THIS source's metaobject (it has one here).
	virtual const ibMetaData* GetSourceMetaData() const override { const auto* mo = GetMetaObject(); return mo != nullptr ? mo->GetMetaData() : nullptr; }

	//Get ref class 
	virtual ibClassID GetSourceClassType() const final { return GetClassType(); }

	//Get presentation 
	virtual wxString GetSourceCaption() const {
		return GetMetaObject() ?
			stringUtils::GenerateSynonym(GetMetaObject()->GetClassName()) + wxT(": ") + GetMetaObject()->GetSynonym() : GetString();
	}

	//Is new object? 
	virtual bool IsNewObject() const { return false; }

	//get unique identifier 
	virtual ibUniqueKey GetGuid() const { return m_objGuid; };

	//get metaData from object 
	virtual const ibValueMetaObjectGenericData* GetMetaObject() const = 0;

	//counter
	virtual void SourceIncrRef() { ibValue::IncrRef(); }
	virtual void SourceDecrRef() { ibValue::DecrRef(); }

	virtual bool IsEmpty() const { return false; }

	//Get ref class 
	virtual ibClassID GetClassType() const = 0;

	virtual wxString GetClassName() const = 0;
	virtual wxString GetString() const = 0;

protected:
	ibGuid m_objGuid;
	ibValuePtr<ibValueDataObjectListColumnCollection> m_recordColumnCollection;

	// L5 — the list's data composer. The source is wired ONCE in the ctor (a
	// list IS metaobject-bound); the settings (user filters / sorts) are
	// re-applied per fetch; the fetch driver carries the page envelope. The
	// build-once page cache (Lever 1) lives INSIDE the composer now.
	mutable ibDataComposer m_composer;
};

// list enumeration
class BACKEND_API ibValueListDataObjectEnumRef : public ibValueListDataObject {
	public:
	struct ibValueTableEnumRow : public ibValueTableRow {
		ibValueTableEnumRow(const ibGuid& guid) :
			ibValueTableRow(), m_objGuid(guid) {
		}
		ibGuid GetGuid() const {
			return m_objGuid;
		}
		// Logical equality by m_objGuid - see ibValueTableListRow.
		virtual bool IsEqualTo(const ibDataViewObject& other) const override {
			const auto* o = dynamic_cast<const ibValueTableEnumRow*>(&other);
			return o != nullptr && m_objGuid == o->m_objGuid;
		}
	private:
		ibGuid m_objGuid;
	};
public:

	virtual bool UseStandartCommand() const {
		return false;
	}

	virtual ibDataViewItem FindRowValue(const ibValue& varValue, const wxString& colName = wxEmptyString) const;

	//Constructor
	ibValueListDataObjectEnumRef(const ibValueMetaObjectRecordDataEnumRef* metaObject = nullptr, const ibFormID& formType = wxNOT_FOUND, bool choiceMode = false);

	virtual void GetValueByRow(wxVariant& variant,
		const ibDataViewItem& row, unsigned int col) const;
	virtual bool SetValueByRow(const wxVariant& variant,
		const ibDataViewItem& row, unsigned int col) override;

	//support source set/get data
	virtual bool GetValueByMetaID(const ibMetaID& id, ibValue& pvarMetaVal) const override;

	//support source data 
	virtual ibSourceExplorer GetSourceExplorer() const;

	//****************************************************************************
	//*                              Support methods                             *
	//****************************************************************************

	void FillMembers(ibMemberTable& helper) const;

	//****************************************************************************
	//*                              Override attribute                          *
	//****************************************************************************
	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);        //setting attribute
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);                   //attribute value

	virtual bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray);       // method call

	//on activate item
	virtual void ActivateItem(ibBackendValueForm* srcForm,
		const ibDataViewItem& item, unsigned int col) {
		if (m_choiceMode)
			ChooseValue(srcForm);
	}

	//get metaData from object 
	virtual const ibValueMetaObjectRecordDataEnumRef* GetMetaObject() const {
		return m_metaObject;
	};

	//Get ref class 
	virtual ibClassID GetClassType() const;

	virtual wxString GetClassName() const;
	virtual wxString GetString() const;

	//support actionData
	virtual ibActionCollection GetActionCollection(const ibFormID& formType);
	virtual void ExecuteAction(const ibActionID& lNumAction, ibBackendValueForm* srcForm);

	//events:
	virtual void ChooseValue(ibBackendValueForm* srcForm);

	//****************************************************************************
	//*                               Paging                                     *
	//****************************************************************************

	// Single-batch paging - enums are tiny and the parent-position
	// CASE/WHEN order doesn't support stable cursoring.

	// Advertise DbFetch so script-side `For Each` routes through the
	// universal cursor iterator. Filters / Sorting omitted because the
	// CASE/WHEN order is fixed.
	virtual Features GetFeatures() const override {
		Features f;
		f.flags |= Features::DbFetch;
		return f;
	}

	// Universal Get*Fetch - frontend (ibDataViewCtrl) holds the deque,
	// calls these to refill ahead/behind windows. Stateless: each call
	// builds an ibFetchRequest and runs SQL through Fetch() below.
	virtual unsigned int GetFirstFetch(const ibDataViewItem& parent,
		const ibDataViewItem& anchor, int count, ibDataViewItemArray& out) const override;
	virtual unsigned int GetNextFetch(const ibDataViewItem& parent,
		const ibDataViewItem& anchor, int count, ibDataViewItemArray& out) const override;
	virtual unsigned int GetPrevFetch(const ibDataViewItem& parent,
		const ibDataViewItem& anchor, int count, ibDataViewItemArray& out) const override;

private:

	//****************************************************************************
	//*                               Support model                              *
	//****************************************************************************


	ibFetchResponse<ibGuid, ibValueTableEnumRow>
		Fetch(const ibFetchRequest<ibGuid>& req) const;

private:

	bool m_choiceMode;
	const ibValueMetaObjectRecordDataEnumRef* m_metaObject;
};

// list without parent  
class BACKEND_API ibValueListDataObjectRef : public ibValueListDataObject {
	public:
	struct ibValueTableListRow : public ibValueTableRow {
		ibValueTableListRow(const ibGuid& guid) :
			ibValueTableRow(), m_objGuid(guid) {
		}
		ibGuid GetGuid() const {
			return m_objGuid;
		}
		// Logical equality by business GUID - mirrors
		// ibValueTreeListNode.  Lets a stub row carrying only m_objGuid
		// (as built by FindRowValue's SQL-fallback for the post-Save
		// focus-restore path) match fully-materialised rows from
		// PagedBootstrap's fetch under operator== / IsEqualTo without
		// requiring m_nodeValues to be populated.
		virtual bool IsEqualTo(const ibDataViewObject& other) const override {
			const auto* o = dynamic_cast<const ibValueTableListRow*>(&other);
			return o != nullptr && m_objGuid == o->m_objGuid;
		}
	private:
		ibGuid m_objGuid;
	};
public:

	virtual ibDataViewItem FindRowValue(const ibValue& varValue, const wxString& colName = wxEmptyString) const;

	//Constructor
	ibValueListDataObjectRef(const ibValueMetaObjectRecordDataMutableRef* metaObject = nullptr, const ibFormID& formType = wxNOT_FOUND, bool choiceMode = false);

	virtual void GetValueByRow(wxVariant& variant,
		const ibDataViewItem& row, unsigned int col) const;
	virtual bool SetValueByRow(const wxVariant& variant,
		const ibDataViewItem& row, unsigned int col) override;

	//support source set/get data
	virtual bool GetValueByMetaID(const ibMetaID& id, ibValue& pvarMetaVal) const override;

	//support source data 
	virtual ibSourceExplorer GetSourceExplorer() const;

	//****************************************************************************
	//*                              Support methods                             *
	//****************************************************************************

	void FillMembers(ibMemberTable& helper) const;

	//****************************************************************************
	//*                              Override attribute                          *
	//****************************************************************************
	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);        //setting attribute
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);                   //attribute value

	virtual bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray);       // method call

	//on activate item
	virtual void ActivateItem(ibBackendValueForm* srcForm,
		const ibDataViewItem& item, unsigned int col) {
		if (m_choiceMode) {
			ChooseValue(srcForm);
		}
		else {
			EditValue();
		}
	}

	//get metaData from object 
	virtual const ibValueMetaObjectRecordDataRef* GetMetaObject() const { return m_metaObject; }

	//Get ref class 
	virtual ibClassID GetClassType() const;

	virtual wxString GetClassName() const;
	virtual wxString GetString() const;

	//support actionData
	virtual ibActionCollection GetActionCollection(const ibFormID& formType);
	virtual void ExecuteAction(const ibActionID& lNumAction, ibBackendValueForm* srcForm);

	//events:
	virtual void AddValue(unsigned int before = 0) override;
	virtual void CopyValue() override;
	virtual void EditValue() override;
	virtual void DeleteValue() override;

	virtual void MarkAsDeleteValue();
	virtual void ChooseValue(ibBackendValueForm* srcForm);

	//****************************************************************************
	//*                               Paging                                     *
	//****************************************************************************


	// Catalog list - DB-backed flat fetch with user filters and
	// column sorting.  No folder / hierarchy concept (that lives on
	// FolderRef tree); list view is always flat.
	virtual Features GetFeatures() const override {
		Features f;
		f.flags |= Features::DbFetch | Features::Filters | Features::Sorting;
		return f;
	}

	// Universal Get*Fetch - see header at the Enum class declaration.
	virtual unsigned int GetFirstFetch(const ibDataViewItem& parent,
		const ibDataViewItem& anchor, int count, ibDataViewItemArray& out) const override;
	virtual unsigned int GetNextFetch(const ibDataViewItem& parent,
		const ibDataViewItem& anchor, int count, ibDataViewItemArray& out) const override;
	virtual unsigned int GetPrevFetch(const ibDataViewItem& parent,
		const ibDataViewItem& anchor, int count, ibDataViewItemArray& out) const override;

private:

	//****************************************************************************
	//*                               Support model                              *
	//****************************************************************************


	// Cursor-paginated fetch - single SQL point used by Get*Fetch.
	ibFetchResponse<ibGuid, ibValueTableListRow>
		Fetch(const ibFetchRequest<ibGuid>& req) const;

private:

	bool m_choiceMode;

	const ibValueMetaObjectRecordDataMutableRef* m_metaObject;
};

// list register
class BACKEND_API ibValueListRegisterObject : public ibValueListDataObject {
	public:
	// Register row carries TWO maps:
	//  * m_nodeKeys   - identity columns (recorder + line for HasRecorder
	//                   registers, dimensions otherwise).  Stable PK for
	//                   row equality across paged refetch.
	//  * m_nodeValues - resources, inherited from ibValueTableRow.  Set
	//                   from fetch via AppendTableValue, displayed in
	//                   the table.  Empty on a stub built by
	//                   FindRowValue for post-Save focus restore.
	struct ibValueTableKeyRow : public ibValueTableRow {
		ibValueTableKeyRow() :
			ibValueTableRow(), m_nodeKeys() {
		}
		void AppendNodeValue(const ibMetaID& id, const ibValue& variant) { m_nodeKeys.insert_or_assign(id, variant); }
		ibValue& AppendNodeValue(const ibMetaID& id) { return m_nodeKeys[id]; }

		const ibRowMetaValues& GetNodeKeys() const { return m_nodeKeys; }

		ibUniqueKeyPair GetUniquePairKey(const ibValueMetaObjectRegisterData* metaObject) const {
			return metaObject->CreateUniqueKeyPair(m_nodeKeys);
		}

		// Logical equality by identity keys - mirrors Catalog/Enum's
		// IsEqualTo by m_objGuid.  Default ibValueTableRow::IsEqualTo
		// compares m_nodeValues (resources) which would never match a
		// stub built by FindRowValue (resources empty).  Override to
		// compare on m_nodeKeys so PagedBootstrap's IsEqualTo locates
		// the freshly-fetched row by its identity, not by resource
		// payload.
		virtual bool IsEqualTo(const ibDataViewObject& other) const override {
			const auto* o = dynamic_cast<const ibValueTableKeyRow*>(&other);
			return o != nullptr && m_nodeKeys == o->m_nodeKeys;
		}

	private:
		ibRowMetaValues m_nodeKeys;
	};
public:

	virtual bool UseStandartCommand() const { return !m_metaObject->HasRecorder(); }

	virtual ibDataViewItem FindRowValue(const ibValue& varValue, const wxString& colName = wxEmptyString) const;

	// Effective cursor order = enabled user sort ++ the register's identity tail
	// (recorder+line / period?+dimensions), via the L3 door. The keyset anchor is
	// built by iterating this so its per-column values line up with the order the
	// door's anchor predicate binds them.
	std::vector<ibQuerySortItem> EffectiveSortOrder() const;

	//Constructor
	ibValueListRegisterObject(const ibValueMetaObjectRegisterData* metaObject = nullptr, const ibFormID& formType = wxNOT_FOUND);

	virtual void GetValueByRow(wxVariant& variant,
		const ibDataViewItem& row, unsigned int col) const;
	virtual bool SetValueByRow(const wxVariant& variant,
		const ibDataViewItem& row, unsigned int col) override;

	//support source set/get data
	virtual bool GetValueByMetaID(const ibMetaID& id, ibValue& pvarMetaVal) const override;

	//support source data 
	virtual ibSourceExplorer GetSourceExplorer() const;

	//****************************************************************************
	//*                              Support methods                             *
	//****************************************************************************

	void FillMembers(ibMemberTable& helper) const;
	virtual bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray);       // method call

	//****************************************************************************
	//*                              Override attribute                          *
	//****************************************************************************
	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);        //setting attribute
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);                   //attribute value

	//on activate item
	virtual void ActivateItem(ibBackendValueForm* srcForm,
		const ibDataViewItem& item, unsigned int col) {
		EditValue();
	}

	//get metaData from object 
	virtual const ibValueMetaObjectRegisterData* GetMetaObject() const {
		return m_metaObject;
	};

	//Get ref class 
	virtual ibClassID GetClassType() const;

	virtual wxString GetClassName() const;
	virtual wxString GetString() const;

	//support actionData
	virtual ibActionCollection GetActionCollection(const ibFormID& formType);
	virtual void ExecuteAction(const ibActionID& lNumAction, ibBackendValueForm* srcForm);

	//events:
	virtual void AddValue(unsigned int before = 0) override;
	virtual void CopyValue() override;
	virtual void EditValue() override;
	virtual void DeleteValue() override;

	//****************************************************************************
	//*                               Paging                                     *
	//****************************************************************************

	// Cursor-paginated.  Effective ORDER BY = [user sorts] ++ [identity
	// tail], so the anchor (ibUniqueKeyPair + sort values tuple) gives
	// stable forward / backward cursoring even when the user has
	// disabled every column-sort.  See EffectiveSortOrder / the L5 fetch.
	virtual Features GetFeatures() const override {
		Features f;
		f.flags |= Features::DbFetch | Features::Filters | Features::Sorting;
		return f;
	}

	virtual unsigned int GetFirstFetch(const ibDataViewItem& parent,
		const ibDataViewItem& anchor, int count, ibDataViewItemArray& out) const override;
	virtual unsigned int GetNextFetch(const ibDataViewItem& parent,
		const ibDataViewItem& anchor, int count, ibDataViewItemArray& out) const override;
	virtual unsigned int GetPrevFetch(const ibDataViewItem& parent,
		const ibDataViewItem& anchor, int count, ibDataViewItemArray& out) const override;

private:

	//****************************************************************************
	//*                               Support model                              *
	//****************************************************************************


	ibFetchResponse<ibUniqueKeyPair, ibValueTableKeyRow>
		Fetch(const ibFetchRequest<ibUniqueKeyPair>& req) const;

private:
	const ibValueMetaObjectRegisterData* m_metaObject;
};

//base tree class 
class BACKEND_API ibValueModelTreeDataObject : public ibValueModelTreeBase,
	public ibSourceDataObject {
	public:
protected:
	enum Func {
		enRefresh
	};
private:

	// implementation of base class virtuals to define model
	virtual ibValueModelColumnCollection* GetColumnCollection() const override { return m_recordColumnCollection; }
	virtual ibValueModelReturnLine* GetRowAt(const ibDataViewItem& line) {
		if (!line.IsOk())
			return nullptr;
		return new ibValueDataObjectTreeReturnLine(this, line);
	}

public:

	class ibValueDataObjectTreeColumnCollection : public ibValueModelTreeBase::ibValueModelColumnCollection {
	public:
		class ibValueDataObjectTreeColumnInfo : public ibValueModelTreeBase::ibValueModelColumnCollection::ibValueModelColumnInfo {
	public:

			virtual unsigned int GetColumnID() const { return m_metaAttribute->GetMetaID(); }
			virtual wxString GetColumnName() const { return m_metaAttribute->GetName(); }
			virtual wxString GetColumnCaption() const { return m_metaAttribute->GetSynonym(); }
			virtual const ibTypeDescription GetColumnType() const { return m_metaAttribute->GetTypeDesc(); }

			ibValueDataObjectTreeColumnInfo();
			ibValueDataObjectTreeColumnInfo(ibValueMetaObjectAttributeBase* attribute);
			virtual ~ibValueDataObjectTreeColumnInfo();

		private:
			ibValueMetaObjectAttributeBase* m_metaAttribute;
		};

	public:

		ibValueDataObjectTreeColumnCollection();
		ibValueDataObjectTreeColumnCollection(ibValueModelTreeDataObject* ownerTable, const ibValueMetaObjectGenericData* metaObject);
		virtual ~ibValueDataObjectTreeColumnCollection();

		virtual const ibTypeDescription GetColumnType(unsigned int col) const {
			ibValueDataObjectTreeColumnInfo* columnInfo = m_listColumnInfo.at(col);
			wxASSERT(columnInfo);
			return columnInfo->GetColumnType();
		}

		virtual ibValueModelColumnInfo* GetColumnInfo(unsigned int idx) const {
			if (m_listColumnInfo.size() < idx)
				return nullptr;
			auto it = m_listColumnInfo.begin();
			std::advance(it, idx);
			return it->second;
		}

		virtual unsigned int GetColumnCount() const {
			const ibValueMetaObjectGenericData* metaTable = m_ownerTable->GetMetaObject();
			wxASSERT(metaTable);
			const auto& obj = metaTable->GetGenericAttributeArrayObject();
			return obj.size();
		}

		//array support 
		virtual bool SetAt(const ibValue& varKeyValue, const ibValue& varValue);
		virtual bool GetAt(const ibValue& varKeyValue, ibValue& pvarValue);

	protected:

		ibValueModelTreeDataObject* m_ownerTable;
		std::map<ibMetaID, ibValuePtr<ibValueDataObjectTreeColumnInfo>> m_listColumnInfo;
	};

	class ibValueDataObjectTreeReturnLine : public ibValueModelReturnLine {
	public:

		ibValueDataObjectTreeReturnLine(ibValueModelTreeDataObject* ownerTable = nullptr, const ibDataViewItem& line = ibDataViewItem(nullptr));
		virtual ~ibValueDataObjectTreeReturnLine();

		virtual ibValueModelTreeBase* GetOwnerModel() const {
			return m_ownerTable;
		}


		void FillMembers(ibMemberTable& helper) const;

		virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal); //setting attribute
		virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal); //attribute value

	protected:
		ibValueModelTreeDataObject* m_ownerTable;
	};

public:

	void AppendSort(ibValueMetaObject* metaObject, bool ascending = true, bool use = true, bool system = false) {
		if (metaObject == nullptr)
			return;
		m_sortOrder.AppendSort(
			metaObject->GetMetaID(),
			metaObject->GetName(),
			metaObject->GetSynonym(),
			ascending, use, system
		);
	}

	virtual bool AutoCreateColumn() const { return false; }
	virtual bool EditableLine(const ibDataViewItem& item, unsigned int col) const { return false; }

	//set meta/get meta
	virtual bool SetValueByMetaID(const ibDataViewItem& item, const ibMetaID& id, const ibValue& varMetaVal) { return false; }
	virtual bool GetValueByMetaID(const ibDataViewItem& item, const ibMetaID& id, ibValue& pvarMetaVal) const {
		ibValueTreeNode* node = GetViewData<ibValueTreeNode>(item);
		if (node == nullptr)
			return false;
		return node->GetValue(id, pvarMetaVal);
	}

	//ctor
	ibValueModelTreeDataObject(const ibValueMetaObjectGenericData* metaObject = nullptr, const ibFormID& formType = wxNOT_FOUND, bool choiceMode = false);
	virtual ~ibValueModelTreeDataObject();

	//****************************************************************************
	//*                               Support model                              *
	//****************************************************************************

	//get metaData from object 
	virtual const ibValueMetaObjectGenericData* GetSourceMetaObject() const final { return GetMetaObject(); }
	// Metadata via THIS source's metaobject (it has one here).
	virtual const ibMetaData* GetSourceMetaData() const override { const auto* mo = GetMetaObject(); return mo != nullptr ? mo->GetMetaData() : nullptr; }

	//Get ref class 
	virtual ibClassID GetSourceClassType() const final { return GetClassType(); }

	//Get presentation 
	virtual wxString GetSourceCaption() const {
		return GetMetaObject() ?
			stringUtils::GenerateSynonym(GetMetaObject()->GetClassName()) + wxT(": ") + GetMetaObject()->GetSynonym() : GetString();
	}

	//Is new object?
	virtual bool IsNewObject() const { return false; }

	//get unique identifier 
	virtual ibUniqueKey GetGuid() const { return m_objGuid; }

	//get metaData from object 
	virtual const ibValueMetaObjectGenericData* GetMetaObject() const = 0;

	//counter
	virtual void SourceIncrRef() { ibValue::IncrRef(); }
	virtual void SourceDecrRef() { ibValue::DecrRef(); }

	virtual bool IsEmpty() const { return false; }

	//Get ref class 
	virtual ibClassID GetClassType() const = 0;

	virtual wxString GetClassName() const = 0;
	virtual wxString GetString() const = 0;

protected:

	ibGuid m_objGuid;
	ibValuePtr<ibValueDataObjectTreeColumnCollection> m_recordColumnCollection;

	// L5 — the tree's data composer (see the list base above): source once in
	// the ctor, settings per fetch, the envelope on the fetch driver.
	mutable ibDataComposer m_composer;
};

// tree with parent or only parent 
class BACKEND_API ibValueModelTreeDataObjectFolderRef : public ibValueModelTreeDataObject {
	public:

	enum {
		LIST_FOLDER,
		LIST_ITEM_FOLDER,
		LIST_ITEM,
	};

	struct ibValueTreeListNode : public ibValueTreeNode {
		// Lazy-load state for the node's own children - set Loaded once
		// FetchChildrenForNode has populated m_children at least once.
		// mutable so const GetChildren() can drive the fetch on first
		// expand without const_cast'ing the whole node.
		enum class LoadState : uint8_t { NotLoaded, Loading, Loaded };

		ibGuid GetGuid() const { return m_objGuid; }
		LoadState GetLoadState() const  { return m_loadState; }
		void SetLoadState(LoadState s)  { m_loadState = s; }

		ibValueTreeListNode(ibValueTreeNode* parent, const ibGuid& guid, ibValueModelTreeDataObject* treeValue = nullptr, bool container = false) :
			ibValueTreeNode(parent), m_objGuid(guid), m_container(container) {
			m_valueTree = treeValue;
		}
		// Folder flag wins over the base "has children loaded" check -
		// a paged FolderRef row knows it CAN contain children even
		// when m_children isn't populated yet.
		virtual bool IsContainer() const override { return m_container; }

		// Logical equality across re-fetches: a fresh node carrying the
		// same business GUID matches the previously-selected one even
		// when behind a different pointer.  Lets selection / breadcrumb
		// survive PagedRefresh wipes.  Comparison against a non-tree
		// row falls through to false (different shapes never match).
		virtual bool IsEqualTo(const ibDataViewObject& other) const override {
			const auto* o = dynamic_cast<const ibValueTreeListNode*>(&other);
			return o != nullptr && m_objGuid == o->m_objGuid;
		}
	private:
		ibGuid m_objGuid;
		bool m_container;
		mutable LoadState m_loadState = LoadState::NotLoaded;
	};

public:

	virtual ibDataViewItem FindRowValue(const ibValue& varValue, const wxString& colName = wxEmptyString) const;

	// Folder-first sort: containers (folders) bubble to the top of
	// every group regardless of column-sort direction.  Falls back to
	// the base tree-model Compare for the secondary ordering.
	virtual int Compare(const ibDataViewItem& item1, const ibDataViewItem& item2,
		unsigned int col, bool ascending) const override {
		const bool c1 = item1.IsContainer();
		const bool c2 = item2.IsContainer();
		if (c1 != c2) return c1 ? -1 : 1;
		return ibValueModelTreeBase::Compare(item1, item2, col, ascending);
	}

	//Constructor
	ibValueModelTreeDataObjectFolderRef(const ibValueMetaObjectRecordDataHierarchyMutableRef* metaObject = nullptr,
		const ibFormID& formType = wxNOT_FOUND, int listMode = LIST_ITEM, bool choiceMode = false);

	virtual void GetValueByRow(wxVariant& variant,
		const ibDataViewItem& item, unsigned int col) const override;
	virtual bool SetValueByRow(const wxVariant& variant,
		const ibDataViewItem& item, unsigned int col) override;
	virtual bool GetAttrByRow(const ibDataViewItem& WXUNUSED(row), unsigned int WXUNUSED(col),
		ibDataViewItemAttr& WXUNUSED(attr)) const override;

	//support source set/get data
	virtual bool GetValueByMetaID(const ibMetaID& id, ibValue& pvarMetaVal) const override;

	//support source data
	virtual ibSourceExplorer GetSourceExplorer() const;
	
	//****************************************************************************
	//*                              Support methods                             *
	//****************************************************************************
	void FillMembers(ibMemberTable& helper) const;

	//****************************************************************************
	//*                              Override attribute                          *
	//****************************************************************************
	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);

	virtual bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray);

	//on activate item
	virtual void ActivateItem(ibBackendValueForm* srcForm,
		const ibDataViewItem& item, unsigned int col) {
		if (m_choiceMode) {
			ChooseValue(srcForm);
		}
		else {
			EditValue();
		}
	}

	//get metaData from object 
	virtual const ibValueMetaObjectRecordDataHierarchyMutableRef* GetMetaObject() const { return m_metaObject; }

	//Get ref class 
	virtual ibClassID GetClassType() const;

	virtual wxString GetClassName() const;
	virtual wxString GetString() const;

	//support actionData
	virtual ibActionCollection GetActionCollection(const ibFormID& formType);
	virtual void ExecuteAction(const ibActionID& lNumAction, ibBackendValueForm* srcForm);

	//events:
	virtual void AddValue(unsigned int before = 0) override;
	virtual void AddFolderValue(unsigned int before = 0);
	virtual void CopyValue() override;
	virtual void EditValue() override;
	virtual void DeleteValue() override;

private:
	// Three-source parent resolution shared by AddValue / AddFolderValue:
	// selected node's parent (item) or self (folder) ? drill-chain head ?
	// empty (catalog root).  Returns the resolved parent value into outParent.
	void ResolveParentForNew(ibValue& outParent) const;
public:

	virtual void MarkAsDeleteValue();
	virtual void ChooseValue(ibBackendValueForm* srcForm);

	// FolderRef - DB-backed tree with folder concept plus user
	// filters and column sorting.  isFolder ID lets the GUI emit
	// folder-first ORDER BY when rendering as a tree / hierarchy.
	virtual Features GetFeatures() const override {
		Features f;
		f.flags |= Features::DbFetch | Features::Tree
		        |  Features::Filters | Features::Sorting;
		if (auto* mf = m_metaObject->GetDataIsFolder()) {
			f.flags |= Features::Folders;
			f.folderSortID = static_cast<int>(mf->GetMetaID());
		}
		return f;
	}

	//****************************************************************************
	//*                               Hierarchy navigation                       *
	//****************************************************************************

	// Walk parent chain from `fromGuid` upward.  Returns the chain
	// ordered [fromGuid, parent, grandparent, ..., top-most ancestor].
	// Empty result if fromGuid is invalid (root) or no rows match.
	// Used by the breadcrumb / drill-up UI on hierarchical view.
	std::vector<ibGuid> GetAncestorChain(const ibGuid& fromGuid) const;

	// Materialise ibValueTreeListNode objects for each guid in `guids`,
	// in input order.  One SELECT with WHERE uuid IN (.), so the cost
	// is one round-trip regardless of chain depth.  Caller takes
	// ownership of the returned pointers (refcount=1).  Used by the
	// view-mode switch path to populate m_topParentChain when entering
	// Hierarchical from List/Tree (the selected row's ancestors weren't
	// loaded as nodes by the flat fetch - we need their full data here
	// for crumb labels).
	std::vector<ibValueTreeListNode*>
	    LoadRowsByGuids(const std::vector<ibGuid>& guids) const;

	// Universal breadcrumb override - chain GUIDs via GetAncestorChain
	// (cached), materialise rows via LoadRowsByGuids, transfer
	// ownership to ibDataViewItem with the standard adopt dance.
	virtual void BuildAncestorBreadcrumb(const ibDataViewItem& fromRow,
	                                     ibDataViewItemArray& out) const override;

	//****************************************************************************
	//*                               Paged tree fetch                           *
	//****************************************************************************

	// Args for NextFetch / PrevFetch.  All row references are opaque
	// ibDataViewItem (control-side identity); model decodes internally
	// to its row type when needed.  Selection and viewport are
	// distinct - user can scroll without changing the selected row.
	//   m_parent          - scope (invalid item == top-level).
	//   m_currentRow      - user selection; preserved across fetch so
	//                       GUI can re-focus it after the buffer
	//                       updates (positioning target).
	//   m_viewportAnchor  - last (Next) / first (Prev) visible row;
	//                       the SQL cursor.
	//   m_count           - batch size (default 1 for tape-like scroll
	//                       tick; viewport-size for initial open).
	struct ibTreeFetchArgs {
		ibDataViewItem m_parent;
		ibDataViewItem m_currentRow;
		ibDataViewItem m_viewportAnchor;
		int            m_count = 1;
	};

	struct ibTreeFetchResponse {
		std::vector<ibValueTreeListNode*> m_rows;
		bool m_hasMore = false;   // more rows past this batch in the same direction
	};

	// First batch.  Two cases:
	//   * empty m_viewportAnchor - cold open, fetch top of dataset;
	//   * non-empty m_viewportAnchor - restoration fetch (paged Refresh
	//     / sort change), anchor row lands in items[0] via INCLUSIVE
	//     cursor (Reset direction).  Call GetNextFetch (Forward, strict)
	//     for plain forward-scroll page.
	ibTreeFetchResponse GetFirstFetch(const ibTreeFetchArgs& args) const;

	// Next portion forward - rows STRICTLY after m_viewportAnchor under m_parent.
	ibTreeFetchResponse GetNextFetch(const ibTreeFetchArgs& args) const;

	// Previous portion backward - rows before m_viewportAnchor.
	ibTreeFetchResponse GetPrevFetch(const ibTreeFetchArgs& args) const;

private:
	// The ONE fetch body for First (Reset = inclusive >=) / Next (Forward =
	// strict >) / Prev (Backward = inverse scan + buffer reverse) — the
	// direction is envelope state, the composed query decides the rest.
	ibTreeFetchResponse FetchWithDirection(const ibTreeFetchArgs& args,
		ibFetchDirection direction) const;
public:

	// Universal Get*Fetch overrides - adapt the typed ibTreeFetchArgs
	// API above to the non-templated virtual on ibValueModel base so
	// generic frontend (BuildXxxHelper, Walker) reaches the paged path.
	virtual unsigned int GetFirstFetch(const ibDataViewItem& parent,
		const ibDataViewItem& anchor, int count, ibDataViewItemArray& out) const override;
	virtual unsigned int GetNextFetch(const ibDataViewItem& parent,
		const ibDataViewItem& anchor, int count, ibDataViewItemArray& out) const override;
	virtual unsigned int GetPrevFetch(const ibDataViewItem& parent,
		const ibDataViewItem& anchor, int count, ibDataViewItemArray& out) const override;

private:

	bool m_choiceMode; int m_listMode;
	const ibValueMetaObjectRecordDataHierarchyMutableRef* m_metaObject;

	// Ancestor-chain cache.  Control may fire GetAncestorChain
	// repeatedly on the same fromGuid (breadcrumb redraw, drill
	// re-entry) - re-walking the parent chain each time would hit
	// the DB N+1 times for nothing.  Cache is keyed by fromGuid.
	mutable ibGuid              m_chainCachedFor;
	mutable std::vector<ibGuid> m_chainCache;
};

#endif 