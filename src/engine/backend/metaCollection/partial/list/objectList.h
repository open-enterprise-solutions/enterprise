#ifndef _OBJECT_LIST_H__
#define _OBJECT_LIST_H__

#include "backend/metaCollection/partial/commonObject.h"
#include "backend/metaCollection/partial/reference/reference.h"
#include "backend/composition/dataComposer.h"
#include "backend/composition/listFilter.h"   // ibValueListSettings / ibValueSortList — the DEFAULT sort goes here (m_sortOrder abolished)

#include <memory>


//base list class
class BACKEND_API ibValueListDataObject : public ibValueModelCursor,
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

	class ibValueDataObjectListColumnCollection : public ibValueModel::ibValueModelColumnCollection {
	public:
		class ibValueDataObjectListColumnInfo : public ibValueModel::ibValueModelColumnCollection::ibValueModelColumnInfo {
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

		virtual ibValueModel* GetOwnerModel() const {
			return m_ownerTable;
		}


		void FillMembers(ibMemberTable& helper) const;

		virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal); //setting attribute
		virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal); //attribute value

	protected:
		ibValueListDataObject* m_ownerTable;
	};

public:

	virtual bool AutoCreateColumn() const { return false; }
	virtual bool EditableLine(const ibDataViewItem& item, unsigned int col) const { return false; }

	//set meta/get meta
	virtual bool SetValueByMetaID(const ibDataViewItem& item, const ibMetaID& id, const ibValue& varMetaVal) { return false; }
	virtual bool GetValueByMetaID(const ibDataViewItem& item, const ibMetaID& id, ibValue& pvarMetaVal) const {
		ibComposerNode* node = GetViewData<ibComposerNode>(item);
		if (node == nullptr)
			return false;
		return node->GetValue(id, pvarMetaVal);
	}

	// Scalar id primitive — leaf list objects override.
	virtual bool SetValueByMetaID(const ibMetaID& id, const ibValue& varMetaVal) { return false; }
	virtual bool GetValueByMetaID(const ibMetaID& id, ibValue& pvarMetaVal) const { return false; }
	
	// ibSourceDataObject hop gate — delegates to the id primitive above.
	virtual bool SetValueBySourceHop(const ibSourceHop& hop, const ibValue& value) override { return SetValueByMetaID(hop.m_id, value); }
	virtual bool GetValueBySourceHop(const ibSourceHop& hop, ibValue& out) const override { return GetValueByMetaID(hop.m_id, out); }

	//ctor
	// `queryable` is the source HOLDER — vended by the SUBCLASS metaobject (the generic base has no GetQueryable);
	// the subclass passes metaObject->GetQueryable() so the composer source is wired ONCE on the base.
	ibValueListDataObject(const ibValueMetaObjectGenericData* metaObject = nullptr, const ibBackendQueryable* queryable = nullptr, const ibFormID& formType = wxNOT_FOUND, bool choiceMode = false);
	virtual ~ibValueListDataObject();

	// Key CREATION lives on the LIST (the base model / cursor produce NO key). A regular list's row key = its
	// primary-key REFERENCE (guid); the register subclass overrides to build its dimension composite instead.
	ibUniqueKey GetItemKey(const ibDataViewItem& item) const override;

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

	// L5 — the list uses the ONE base composer (GetModelComposer(), points 7+8: no subclass holds its
	// own). Source wired ONCE in the ctor; settings re-applied per fetch; the fetch driver carries the
	// page envelope; the build-once page cache (Lever 1) lives inside that composer.
};

// list enumeration
class BACKEND_API ibValueListDataObjectEnumRef : public ibValueListDataObject {
	public:
	// (ibValueTableEnumRow DELETED — the enum fetches through RunComposerPage like every other list now; its
	//  rows come back as ibComposerNode, keyed by the reference row-key.)

	virtual bool UseStandartCommand() const {
		return false;
	}

	//Constructor
	ibValueListDataObjectEnumRef(const ibValueMetaObjectRecordDataEnumRef* metaObject = nullptr, const ibFormID& formType = wxNOT_FOUND, bool choiceMode = false);

	// Enum source = the metaobject's queryable. An enumeration is just a DB source whose data is a single
	// STATIC set defined in the configurator (Max) — accessed exactly like catalog/register, so the universal
	// RunComposerPage reads it through this with no special handling.
	virtual const ibBackendQueryable* GetSourceQueryable() const override { return m_metaObject->GetQueryable(); }

	virtual void GetValueByRow(wxVariant& variant,
		const ibDataViewItem& row, unsigned int col) const;
	virtual bool SetValueByRow(const wxVariant& variant,
		const ibDataViewItem& row, unsigned int col) override;

	//support source set/get data
	virtual bool GetValueByMetaID(const ibMetaID& id, ibValue& pvarMetaVal) const override;

	//support source data 
	virtual const ibSourceExplorer* GetSourceExplorer() const;

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

	// (Paging GONE — the enum reads through the base ibValueModel::RunComposerPage over GetSourceQueryable()
	//  (its metaobject's queryable); GetFirstFetch/Next/Prev + the SQL Fetch are deleted; the ctor sets the
	//  position sort on the composer.) An enum is a normal keyed L5 list now — same features as a catalog list:
	//  Filters + Sorting + Grouping (all driven by the one composer; paging + keyed-ness are derived, not flags).
	virtual Features GetFeatures() const override {
		Features f;
		f.flags |= Features::Filters | Features::Sorting | Features::Grouping;
		return f;
	}

private:

	bool m_choiceMode;
	const ibValueMetaObjectRecordDataEnumRef* m_metaObject;
};

// list without parent
class BACKEND_API ibValueListDataObjectRef : public ibValueListDataObject {
public:
	// (No FindRowValue override — the catalog's rows are ibComposerNode keyed by m_rowKey = the
	//  reference value, which is exactly the base ibValueModel::FindRowValue key-stub. Same for the enum and
	//  the folder tree; only the register, whose key is its PK columns, still overrides.)

	//Constructor
	ibValueListDataObjectRef(const ibValueMetaObjectRecordDataMutableRef* metaObject = nullptr, const ibFormID& formType = wxNOT_FOUND, bool choiceMode = false);

	virtual void GetValueByRow(wxVariant& variant,
		const ibDataViewItem& row, unsigned int col) const;
	virtual bool SetValueByRow(const wxVariant& variant,
		const ibDataViewItem& row, unsigned int col) override;

	//support source set/get data
	virtual bool GetValueByMetaID(const ibMetaID& id, ibValue& pvarMetaVal) const override;

	//support source data 
	virtual const ibSourceExplorer* GetSourceExplorer() const;

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
	//*                              Fetch / features                            *
	//****************************************************************************


	// Catalog list - DB-backed flat fetch with user filters and
	// column sorting.  No folder / hierarchy concept (that lives on
	// FolderRef tree); list view is always flat.
	virtual Features GetFeatures() const override {
		Features f;
		f.flags |= Features::Filters | Features::Sorting | Features::Grouping;
		return f;
	}

	// FOLD (#2): the catalog list reads THROUGH the universal base fetch (ibValueModel::RunComposerPage)
	// now — it vends its metaobject's queryable and lets the base page it via the L5 composer + the keyset
	// cursor (the anchor carries the user sort values + the source's GetIdentitySort tail). The per-list
	// Get*Fetch / Fetch / BuildRefAnchor are GONE (base code now); the rows come back as ibComposerNode
	// (value-map identity, the reference column rides in the values → selection survives).
	virtual const ibBackendQueryable* GetSourceQueryable() const override { return m_metaObject->GetQueryable(); }

private:

	// (The per-list cursor Fetch is GONE — folded into ibValueModel::RunComposerPage, see tableInfo.cpp.)

	bool m_choiceMode;

	const ibValueMetaObjectRecordDataMutableRef* m_metaObject;
};

// list register
class BACKEND_API ibValueListRegisterObject : public ibValueListDataObject {
	public:
	// (ibValueTableKeyRow DELETED — the register's rows are ibComposerNode now; its composite
	//  identity is the composer node's m_rowKey = the primary-key (recorder+line / dimensions) values that
	//  RunComposerPage stamps. FindRowValue builds a composer stub with that row-key; GetItemKey (override below)
	//  reads the item's dimensions into the key the selection ops feed the record manager.)
public:

	virtual bool UseStandartCommand() const { return !m_metaObject->HasRecorder(); }

	// The register row key = its DIMENSIONS (composite), not a single reference guid — override the cursor default.
	ibUniqueKey GetItemKey(const ibDataViewItem& item) const override;

	virtual ibDataViewItem FindRowValue(const ibValue& varValue, const wxString& colName = wxEmptyString) const;

	//Constructor
	ibValueListRegisterObject(const ibValueMetaObjectRegisterData* metaObject = nullptr, const ibFormID& formType = wxNOT_FOUND);

	virtual void GetValueByRow(wxVariant& variant,
		const ibDataViewItem& row, unsigned int col) const;
	virtual bool SetValueByRow(const wxVariant& variant,
		const ibDataViewItem& row, unsigned int col) override;

	//support source set/get data
	virtual bool GetValueByMetaID(const ibMetaID& id, ibValue& pvarMetaVal) const override;

	//support source data 
	virtual const ibSourceExplorer* GetSourceExplorer() const;

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
	//*                              Fetch / features                            *
	//****************************************************************************

	// Cursor-paginated.  Effective ORDER BY = [user sorts] ++ [identity
	// tail], so the anchor (ibUniqueKeyPair + sort values tuple) gives
	// stable forward / backward cursoring even when the user has
	// disabled every column-sort.  See GetIdentitySort / RunComposerPage.
	virtual Features GetFeatures() const override {
		Features f;
		f.flags |= Features::Filters | Features::Sorting | Features::Grouping;
		return f;
	}

	// FOLD (#2): the register reads THROUGH the base RunComposerPage now — it vends its metaobject's
	// queryable; the base keyset cursor carries the user sort values + the register's identity tail
	// (recorder+line / period?+dimensions) via GetIdentitySort. The per-list Get*Fetch / Fetch /
	// BuildRegisterAnchor / EffectiveSortOrder are GONE (base code); rows come back as ibComposerNode
	// (value-map identity over the composite key columns, which ride in the row values).
	virtual const ibBackendQueryable* GetSourceQueryable() const override { return m_metaObject->GetQueryable(); }

private:
	const ibValueMetaObjectRegisterData* m_metaObject;
};

//base tree class 
class BACKEND_API ibValueModelTreeDataObject : public ibValueModelCursor,
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

	// Key CREATION lives on the tree LIST (the model / cursor produce NO key): a folder row's key = its
	// primary-key REFERENCE (guid), same shape as a flat list.
	ibUniqueKey GetItemKey(const ibDataViewItem& item) const override;

	class ibValueDataObjectTreeColumnCollection : public ibValueModelCursor::ibValueModelColumnCollection {
	public:
		class ibValueDataObjectTreeColumnInfo : public ibValueModelCursor::ibValueModelColumnCollection::ibValueModelColumnInfo {
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

		virtual ibValueModelCursor* GetOwnerModel() const {
			return m_ownerTable;
		}


		void FillMembers(ibMemberTable& helper) const;

		virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal); //setting attribute
		virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal); //attribute value

	protected:
		ibValueModelTreeDataObject* m_ownerTable;
	};

public:

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

	// Scalar id primitive — leaf tree objects override.
	virtual bool SetValueByMetaID(const ibMetaID& id, const ibValue& varMetaVal) { return false; }
	virtual bool GetValueByMetaID(const ibMetaID& id, ibValue& pvarMetaVal) const { return false; }

	// ibSourceDataObject hop gate — delegates to the id primitive above.
	virtual bool SetValueBySourceHop(const ibSourceHop& hop, const ibValue& value) override { return SetValueByMetaID(hop.m_id, value); }
	virtual bool GetValueBySourceHop(const ibSourceHop& hop, ibValue& out) const override { return GetValueByMetaID(hop.m_id, out); }

	//ctor
	ibValueModelTreeDataObject(const ibValueMetaObjectGenericData* metaObject = nullptr, const ibBackendQueryable* queryable = nullptr, const ibFormID& formType = wxNOT_FOUND, bool choiceMode = false);
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

	// L5 — the tree uses the ONE base composer (GetModelComposer(), points 7+8): source once in the
	// ctor, settings per fetch, the envelope on the fetch driver. No subclass holds its own composer.
};

// tree with parent or only parent 
class BACKEND_API ibValueModelTreeDataObjectFolderRef : public ibValueModelTreeDataObject {
	public:

	enum {
		LIST_FOLDER,
		LIST_ITEM_FOLDER,
		LIST_ITEM,
	};

	// (ibValueTreeListNode DELETED — the folder catalog's rows are ibComposerNode now, built by the
	//  base RunComposerPage; nothing needs a typed guid-keyed tree node any more.)

public:

	// Folder-first sort: containers (folders) bubble to the top of
	// every group regardless of column-sort direction.  Falls back to
	// the base tree-model Compare for the secondary ordering.
	virtual int Compare(const ibDataViewItem& item1, const ibDataViewItem& item2,
		unsigned int col, bool ascending) const override {
		const bool c1 = item1.IsContainer();
		const bool c2 = item2.IsContainer();
		if (c1 != c2) return c1 ? -1 : 1;
		return ibValueModelCursor::Compare(item1, item2, col, ascending);
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
	virtual const ibSourceExplorer* GetSourceExplorer() const;

	// The catalog's metaobject queryable IS the source: it carries the HIERARCHY (GetHierarchyColumn) and
	// FOLDER (GetFolderColumn) columns the base RunComposerPage reads for the parent-tree scope + the folder
	// container flag. Without this override the base returned the RAM queryable (none of those) → no tree.
	virtual const ibBackendQueryable* GetSourceQueryable() const override { return m_metaObject->GetQueryable(); }

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

	// FolderRef — same user-facing features as any catalog list (Filters / Sorting / Grouping). Its tree shape is
	// the DEFAULT Hierarchy grouping set in the ctor (tree-ness = composer grouping, not a flag); folder-first
	// ORDER is a composer-grouping concern, so there is no Folders flag here any more.
	virtual Features GetFeatures() const override {
		Features f;
		f.flags |= Features::Filters | Features::Sorting | Features::Grouping;
		return f;
	}

	// (Hierarchy navigation + paged-tree-fetch args GONE — no GetAncestorChain / LoadRowsByGuids /
	//  ibTreeFetchArgs / ancestor cache, and there is no breadcrumb any more. The folder catalog is JUST the
	//  composer's Hierarchy grouping; the base ibValueModel::RunComposerPage builds the tree, and "you are here"
	//  is the one mechanism rebuilding flat ↔ grouping ↔ hierarchy by the composer's value, not a separate
	//  ancestor walk. Max: "breadcrumbs aren't needed — it already works that way".)

private:

	bool m_choiceMode; int m_listMode;
	const ibValueMetaObjectRecordDataHierarchyMutableRef* m_metaObject;
};

#endif 