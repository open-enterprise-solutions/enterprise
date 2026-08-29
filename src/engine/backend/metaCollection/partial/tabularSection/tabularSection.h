#ifndef _VALUETABLEPART_H__

#include "backend/tabularModel.h"
#include "backend/picturePredefined.h"                 // g_pic*CLSID — this model emits its own standard command icons
#include "backend/valueInfo.h"

#include "backend/metaCollection/table/metaTableObject.h"

class BACKEND_API ibValueTabularSectionDataObjectBase : public ibValueModelStorage {
	public:
private:

	enum Func {
		enAddValue = 0,
		enCount,
		enFind,
		enDelete,
		enClear,
		enLoad,
		enUnload,
		enGetMetadata,
	};

	enum {
		eTabularSection,
	};

	class ibVariantDataValueNumberLine :
		public ibVariantDataValueImpl<ibValue> {
	public:
		ibVariantDataValueNumberLine(const long& cValue)
			: ibVariantDataValueImpl(static_cast<signed int>(cValue))
		{
		}
	};

public:

	// NOT transferable across sessions. A tabular section is not a table of its
	// own — it is PART of an object (m_objectValue below), the lines of a document
	// or a catalog item that is open and being edited. Handing it over would hand
	// a second session a live piece of somebody else's object, which is refused
	// for the same reason the object itself is.
	//
	// A value table copied out of it (Unload) travels perfectly well: at that
	// point the rows are a self-contained runtime value and belong to nobody.
	virtual bool IsTransferable() const override { return false; }

	virtual ibValueModelColumnCollection* GetColumnCollection() const override { return m_recordColumnCollection; }
	virtual ibValueModelReturnLine* GetRowAt(const ibDataViewItem& line) {
		if (!line.IsOk())
			return nullptr;
		return new ibValueTabularSectionDataObjectReturnLine(this, line);
	}

	virtual bool HasDefaultCompare() const override { return false; }

	// TabularSection — RAM-backed (inherited) + user filter row +
	// column sort UI.  BuildVisibleView on the base picks up
	// m_filterRow / m_sortOrder so the GUI's filter/sort affordances
	// drive Get*Fetch slicing automatically.
	virtual Features GetFeatures() const override {
		auto f = ibValueModelStorage::GetFeatures();
		f.flags |= Features::Filters | Features::Sorting | Features::Grouping;
		return f;
	}

	virtual ibDataViewItem FindRowValue(const ibValue& varValue, const wxString& colName = wxEmptyString) const;

	//set meta/get meta
	virtual bool SetValueByMetaID(const ibDataViewItem& item, const ibMetaID& id, const ibValue& varMetaVal);
	virtual bool GetValueByMetaID(const ibDataViewItem& item, const ibMetaID& id, ibValue& pvarMetaVal) const;

	virtual ibValue GetValueByMetaID(const ibDataViewItem& item, const ibMetaID& id) const {
		ibValue retValue;
		if (GetValueByMetaID(item, id, retValue))
			return retValue;
		return ibValue();
	}

	class ibValueTabularSectionDataObjectColumnCollection : public ibValueModel::ibValueModelColumnCollection {
	public:
		class ibValueTabularSectionColumnInfo : public ibValueModel::ibValueModelColumnCollection::ibValueModelColumnInfo {
	public:

			virtual unsigned int GetColumnID() const { return m_metaAttribute->GetMetaID(); }
			virtual wxString GetColumnName() const { return m_metaAttribute->GetName(); }
			virtual wxString GetColumnCaption() const { return m_metaAttribute->GetSynonym(); }
			virtual const ibTypeDescription GetColumnType() const { return m_metaAttribute->GetTypeDesc(); }

			ibValueTabularSectionColumnInfo();
			ibValueTabularSectionColumnInfo(ibValueMetaObjectAttributeBase* attribute);
			virtual ~ibValueTabularSectionColumnInfo();

		private:
			ibValueMetaObjectAttributeBase* m_metaAttribute;
			friend ibValueTabularSectionDataObjectColumnCollection;
		};

	public:

		ibValueTabularSectionDataObjectColumnCollection();
		ibValueTabularSectionDataObjectColumnCollection(ibValueTabularSectionDataObjectBase* ownerTable);
		virtual ~ibValueTabularSectionDataObjectColumnCollection();

		virtual ibValueModelColumnInfo* GetColumnInfo(unsigned int idx) const {
			if (idx >= m_listColumnInfo.size())   // `size() < idx` let idx == size() through onto end()
				return nullptr;
			auto it = m_listColumnInfo.begin();
			std::advance(it, idx);
			return it->second;
		}

		virtual unsigned int GetColumnCount() const { return m_listColumnInfo.size(); }

		//array support 
		virtual bool SetAt(const ibValue& varKeyValue, const ibValue& varValue);
		virtual bool GetAt(const ibValue& varKeyValue, ibValue& pvarValue);

		friend class ibValueTabularSectionDataObjectBase;

	protected:

		ibValueTabularSectionDataObjectBase* m_ownerTable;
		std::map<ibMetaID, ibValuePtr<ibValueTabularSectionColumnInfo>> m_listColumnInfo;
	};

	class ibValueTabularSectionDataObjectReturnLine : public ibValueModelReturnLine {
	public:

		ibValueTabularSectionDataObjectReturnLine(ibValueTabularSectionDataObjectBase* ownerTable = nullptr, const ibDataViewItem& line = ibDataViewItem(nullptr));
		virtual ~ibValueTabularSectionDataObjectReturnLine();

		virtual ibValueModel* GetOwnerModel() const { return m_ownerTable; }

		void FillMembers(ibMemberTable& helper) const;   // bound in ctor (was PrepareNames)

		virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal); //setting attribute
		virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal); //attribute value

		//Get ref class
		virtual ibClassID GetClassType() const;

		virtual wxString GetClassName() const;
		virtual wxString GetString() const;

		friend class ibValueTabularSectionDataObjectBase;
	private:
		ibValueTabularSectionDataObjectBase* m_ownerTable;
	};

	const ibValueMetaObjectTableData* GetMetaObject() const { return m_metaTable; }
	ibMetaID GetMetaID() const { return m_metaTable ? m_metaTable->GetMetaID() : wxNOT_FOUND; }

#pragma region _source_data_

	//get metaData from object
	virtual const ibValueMetaObjectCompositeData* GetSourceMetaObject() const { return m_metaTable; }

	//get metaData: the section's own config via its meta object; no object -> the active config, so reference-
	//typed columns still resolve their targets. Mirrors ibValueModelTable / the object list.
	virtual const ibMetaData* GetSourceMetaData() const override {
		const ibValueMetaObjectCompositeData* mo = GetSourceMetaObject();
		return mo != nullptr ? mo->GetMetaData() : nullptr;
	}

	//Get ref class
	virtual ibClassID GetSourceClassType() const { return GetClassType(); }

#pragma endregion 

	ibValueTabularSectionDataObjectBase() :
		m_readOnly(false), m_metaTable(nullptr),
		m_objectValue(nullptr),
		m_recordColumnCollection(nullptr) {
		m_members.Bind(this, &ibValueTabularSectionDataObjectBase::FillMembers);
	}

	ibValueTabularSectionDataObjectBase(ibValueDataObject* objectValue, const ibValueMetaObjectTableData* tableObject, bool readOnly = false) :
		m_readOnly(readOnly), m_metaTable(tableObject),
		m_objectValue(objectValue),
		m_recordColumnCollection(new ibValueTabularSectionDataObjectColumnCollection(this)) {
		m_members.Bind(this, &ibValueTabularSectionDataObjectBase::FillMembers);
		// (The RAM composer is auto-bound to this model's value-storage in ibValueModelStorage's ctor — no manual
		// source binding needed; ibDataRamComposer reads the storage's nodes in place.)
		// (No m_filterRow template population — the filter lives in L5 (ListSettings->Filter) now; the
		// filter dialog offers the columns from the metadata / column collection directly.)
	}

	virtual ~ibValueTabularSectionDataObjectBase() {}

	virtual void GetValueByRow(wxVariant& variant,
		const ibDataViewItem& row, unsigned int col) const override;
	virtual bool SetValueByRow(const wxVariant& variant,
		const ibDataViewItem& row, unsigned int col) override;

	// ⭐⭐ THE NUMBER A PERSON READS — the row's place in WHAT IS SHOWN, 1-based; 0 when the row is not shown
	// (a group header, a row outside the filter, a row of another table). It says how the rows stand one
	// after another right now, and it must read 1, 2, 3, 4, 5, 6 however the order has been played with
	// (Max, 2026-08-29).
	//
	// 🛑 It used to be the STORAGE index. Truthful about the data and wrong on the screen: under a sort it
	// came out jumbled — 1,2,3,4,6,5 — and under a filter it counted rows nobody could see. The stored line
	// number is still the storage order (that is what SaveData writes); this column is simply not that.
	//
	// The display order is the composer's, so it is asked of the composer — and cached against the VIEW
	// GENERATION, because it is asked once per painted cell and the answer only moves when the view does.
	long DisplayNumberOf(const ibDataViewItem& row) const;

	virtual bool AutoCreateColumn() const { return false; }
	virtual bool EditableLine(const ibDataViewItem& item, unsigned int col) const {
		return ibValueModel::EditableLine(item, col) && !m_metaTable->IsNumberLine(col);
	}


	// `row` = WHERE it goes (the selection); `anchor` = the group it is INSIDE, whose dimensions it inherits.
	// Empty anchor (the root of a hierarchical view) = inherits nothing and forms its own empty group.
	void AddValue(const ibDataViewItem& row, const ibDataViewItem& anchor = ibDataViewItem());
	virtual void CopyValue(const ibDataViewItem& row);
	void EditValue(const ibDataViewItem& row);
	virtual void DeleteValue(const ibDataViewItem& row);
	// VIRTUAL for the same reason Copy and Delete are: a section that belongs to a REFERENCE object has to
	// mark that object modified, and the base has no object to mark. The bodies are one line each — the RAM
	// base's physical call — and the Ref subclass wraps them.
	virtual void MoveValue(const ibDataViewItem& row, int delta);
	virtual void SortValue(const ibDataViewColumnItem& column, bool ascending);

	// Command store (ibStandardCommandTabular): a tabular section defines its OWN Add / Copy / Edit / Delete and runs
	// them by id on the front-passed row (no shared base set — each model ships its own).
	enum { eAddValue = 1, eCopyValue, eEditValue = 3 | eStartEditingFlag, eDeleteValue = 4,
		eMoveUpValue = 5, eMoveDownValue, eSortAscValue, eSortDescValue };   // Edit's id carries the front-edit flag
	virtual void GetCommandCollection(const ibFormID& formType, std::vector<ibCommandItem>& commands) const override {
		commands.emplace_back(eAddValue,    wxT("Add"),    _("Add"),    g_picAddCLSID,    true);
		commands.emplace_back(eCopyValue,    wxT("Copy"),   _("Copy"),   g_picCopyCLSID);
		commands.emplace_back(eEditValue,    wxT("Edit"),   _("Edit"),   g_picEditCLSID);
		commands.emplace_back(eDeleteValue,  wxT("Delete"), _("Delete"), g_picDeleteCLSID);
		commands.emplace_back();   // separator — what follows is about the ORDER, not about the rows
		commands.emplace_back(eMoveUpValue,   wxT("MoveUp"),   _("Move up"),   g_picMoveUpCLSID);
		commands.emplace_back(eMoveDownValue, wxT("MoveDown"), _("Move down"), g_picMoveDownCLSID);
		// …and a separator BETWEEN THE TWO PAIRS: moving is done BY HAND to one row, ordering is done BY
		// A COLUMN to all of them — two questions, two groups.
		commands.emplace_back();
		// All four keep the DEFAULT modify flag (Max, 2026-08-29): in a view-only form they show and grey out.
		commands.emplace_back(eSortAscValue,  wxT("SortAsc"),  _("Sort ascending"),  g_picSortAscCLSID);
		commands.emplace_back(eSortDescValue, wxT("SortDesc"), _("Sort descending"), g_picSortDescCLSID);
	}
	// Flat tabular section: no hierarchy, so only the SELECTED row matters — m_anchor is ignored.
	virtual void CallAsCommand(const ibActionID& lNumAction, const ibDataViewCommandContext& ctx, ibBackendValueForm* srcForm) override {
		switch (lNumAction) {
		case eAddValue:    AddValue(ctx.m_selection, ctx.m_anchor);   break;   // where it goes · what it inherits
		case eCopyValue:   CopyValue(ctx.m_selection);   break;
		case eEditValue:   EditValue(ctx.m_selection);   break;   // no-op on the backend; Edit's id carries eStartEditingFlag → the FRONT opens the real inline editor
		case eDeleteValue: DeleteValue(ctx.m_selection); break;
		// The four order verbs — through this class's own virtuals, so a section owned by a reference object
		// can mark it modified, exactly as Copy and Delete do.
		case eMoveUpValue:   MoveValue(ctx.m_selection, -1); break;
		case eMoveDownValue: MoveValue(ctx.m_selection, +1); break;
		case eSortAscValue:  SortValue(ctx.m_column, true);  break;
		case eSortDescValue: SortValue(ctx.m_column, false); break;
		}
	}

	//append new row
	virtual long AppendRow(unsigned int before = 0, const ibDataViewItem& contextRow = ibDataViewItem());

	virtual bool LoadData(const ibGuid& srcGuid, bool createData = true) { return true; }
	virtual bool SaveData() { return true; }
	virtual bool DeleteData() { return true; }

	virtual bool LoadDataFromTable(ibValueModel* srcTable);
	virtual ibValueModel* SaveDataToTable() const;

	//****************************************************************************
	//*                              Support methods                             *
	//****************************************************************************

	void FillMembers(ibMemberTable& helper) const;   // bound in ctor (was PrepareNames)
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);       // method call

	//array
	virtual bool GetAt(const ibValue& varKeyValue, ibValue& pvarValue);

	//Get ref class 
	virtual ibClassID GetClassType() const;

	virtual wxString GetClassName() const;
	virtual wxString GetString() const;

	// Iterator runtime path lives on ibValueModel (cursor over Get*Fetch
	// → BuildVisibleView for filter+sort consistency with the GUI).
	// GetEmptyRow yields the typed skeleton for IntelliSense type hint.
	virtual ibValue GetEmptyRow() override {
		return new ibValueTabularSectionDataObjectReturnLine(this, ibDataViewItem(nullptr));
	}

protected:

	bool m_readOnly;

	// The display order, as of the view generation it was read at — see DisplayNumberOf.
	mutable std::vector<long> m_displayOrder;
	mutable uint32_t          m_displayOrderGeneration = 0;
	mutable bool              m_displayOrderRead = false;

	const ibValueMetaObjectTableData* m_metaTable;

	ibValueDataObject* m_objectValue;
	ibValuePtr<ibValueTabularSectionDataObjectColumnCollection> m_recordColumnCollection;
};

class BACKEND_API ibValueTabularSectionDataObject : public ibValueTabularSectionDataObjectBase {
	public:

	ibValueTabularSectionDataObject();
	ibValueTabularSectionDataObject(class ibValueRecordDataObject* recordObject, const ibValueMetaObjectTableData* tableObject);
	virtual ~ibValueTabularSectionDataObject() {}
};

class BACKEND_API ibValueTabularSectionDataObjectRef : public ibValueTabularSectionDataObjectBase {
	public:

	bool IsReadAfter() const { return m_readAfter; }

	ibValueTabularSectionDataObjectRef();
	ibValueTabularSectionDataObjectRef(class ibValueReferenceDataObject* reference, const ibValueMetaObjectTableData* tableObject, bool readAfter = false);
	ibValueTabularSectionDataObjectRef(class ibValueRecordDataObjectRef* recordObject, const ibValueMetaObjectTableData* tableObject);
	ibValueTabularSectionDataObjectRef(class ibValueSelectorRecordDataObject* selectorObject, const ibValueMetaObjectTableData* tableObject);

	virtual ~ibValueTabularSectionDataObjectRef() {}

	virtual void CopyValue(const ibDataViewItem& row);
	virtual void DeleteValue(const ibDataViewItem& row);
	// ⭐ ORDERING ROWS IS A DATA CHANGE, so the object they belong to is modified by it — exactly as by an
	// add, a copy or a delete. Both verbs re-seat the rows physically, and the row order is what the write
	// stores (SaveData numbers by it), so a document sorted or re-ordered and then closed unsaved would
	// have lost a real edit without ever saying it had one (Max, 2026-08-29).
	virtual void MoveValue(const ibDataViewItem& row, int delta) override;
	virtual void SortValue(const ibDataViewColumnItem& column, bool ascending) override;

	//append new row
	virtual long AppendRow(unsigned int before = 0, const ibDataViewItem& contextRow = ibDataViewItem());

	//load/save/delete data
	virtual bool LoadData(const ibGuid& srcGuid, bool createData = true);
	virtual bool SaveData();
	virtual bool DeleteData();

	//set meta/get meta
	virtual bool SetValueByMetaID(const ibDataViewItem& item, const ibMetaID& id, const ibValue& varMetaVal);
	virtual bool GetValueByMetaID(const ibDataViewItem& item, const ibMetaID& id, ibValue& pvarMetaVal) const;

protected:
	bool m_readAfter;
};

#endif // !_VALUEUUID_H__
