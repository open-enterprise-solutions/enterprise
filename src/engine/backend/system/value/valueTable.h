#ifndef __VALUE_TABLE_H__
#define __VALUE_TABLE_H__

#include "valueArray.h"
#include "valueMap.h"

#include "backend/tableInfo.h"
#include "backend/srcDataObject.h"                    // ibSourceDataObject / ibSourceExplorer — the table IS a form data source
#include "backend/query/queryColumn.h"                 // ibBackendSourceColumn — a RAM column IS its own source-column presentation (header/type via the explorer)
#include "backend/propertyManager/propertyManager.h"  // ibPropertyObject / ibPropertyUString / ibPropertyType — columns surface / persist AND edit with the form attribute
#include "backend/stringUtils.h"                       // stringUtils::GenerateSynonym — Caption-empty header fallback (mirror ibFormAttribute)

#include <memory>
#include <vector>

constexpr ibClassID g_valueTableCLSID = value_to_clsid("VL_TABL");

class ibValueModelTable;

// NOTE: a table-of-values is a RAM model (ibValueModelStorage). It has NO source queryable — the RAM composer
// filters/sorts ibRamValueStorage (the live nodes) in place. There is no RAM query-text / SQL door any more.

//Table support
// The value-table is AT ONCE a RAM runtime model (ibValueModelStorage), a form data SOURCE (ibSourceDataObject —
// its columns feed the dropped tablebox's OWN column picker, per-row), AND a property object (ibPropertyObject —
// its columns SERIALIZE with the form attribute, exactly like ibValueDynamicList's Source/Settings). EXACTLY
// analogous to ibValueDynamicList. ibValue stays the FIRST base via ibValueModelStorage -> ibValueModel ->
// ibValueDynamicMembers -> ibValue (that chain is NOT reordered).
class BACKEND_API ibValueModelTable : public ibValueModelStorage, public ibSourceDataObject, public ibPropertyObject {
	public:
private:
	// methods:
	enum Func {
		enAddRow = 0,
		enClone,
		enCount,
		enFind,
		enDelete,
		enClear,
		enSort,
	};
	//attributes:
	enum Prop {
		enColumns = 0,
	};
public:
	class ibValueModelTableColumnCollection : public ibValueModel::ibValueModelColumnCollection {
	public:
	private:
		enum Func {
			enAddColumn = 0,
			enRemoveColumn
		};
	public:

		// A column is an EDITABLE property object (Name + Caption + Type surface in the designer inspector
		// when a column tree-node is selected) AND a source-column descriptor (ibBackendSourceColumn — the
		// ABSTRACT source column, NOT a QueryColumn: a value-table pulls from RAM, it is not a DB query like
		// the dynamic list) so the bound tablebox reads its header / type through the SAME explorer ->
		// WalkColumns -> GetSourceAbstractColumn presentation seam a metadata field uses — the column's
		// Caption IS its header (GetSynonym). ibValue stays the FIRST base via ibValueModelColumnInfo ->
		// ibValueDynamicMembers -> ibValue (offset 0); ibPropertyObject + ibBackendTypeConfigFactory follow
		// (the base order ibFormAttribute uses); the type factory is mandatory (ibPropertyType resolves its
		// owner through it, else a null variant crash).
		class ibValueModelTableColumnInfo :
			public ibValueModel::ibValueModelColumnCollection::ibValueModelColumnInfo,
			public ibPropertyObject,
			public ibBackendTypeConfigFactory,
			public ibBackendSourceColumn {
		public:

			ibValueModelTableColumnInfo();
			ibValueModelTableColumnInfo(unsigned int colId, const wxString& colName, const ibTypeDescription& typeDescription, const wxString& caption, int width);
			virtual ~ibValueModelTableColumnInfo();

			// --- column accessors — name / caption / type read & write THROUGH the property variant (the
			// single source of truth); id + width are plain identity / layout members. ------------------------
			virtual unsigned int GetColumnID() const { return m_columnID; }
			virtual void SetColumnID(unsigned int col) { m_columnID = col; }
			virtual wxString GetColumnName() const { return m_propertyName->GetValueAsString(); }
			virtual void SetColumnName(const wxString& name) { m_propertyName->SetValue(name); }
			virtual wxString GetColumnCaption() const { return m_propertyCaption->GetValueAsTranslateString(); }
			virtual void SetColumnCaption(const wxString& caption) { m_propertyCaption->SetValue(caption); }
			virtual const ibTypeDescription GetColumnType() const { return m_propertyType->GetValueAsTypeDesc(); }
			virtual void SetColumnType(const ibTypeDescription& typeDescription) { m_propertyType->SetValue(typeDescription); }
			virtual int GetColumnWidth() const { return m_columnWidth; }
			virtual void SetColumnWidth(int width) { m_columnWidth = width; }

			// --- ibPropertyObject — inspector identity. GetClassName resolves through the registered clsid
			// (VL_TVCLI) and DISAMBIGUATES the two same-name bases (ibValue + ibPropertyObject). --------------
			virtual wxString GetClassName() const override { return ibValue::GetClassName(); }
			virtual wxString GetObjectTypeName() const override { return GetColumnName(); }
			virtual bool IsEditable() const override { return true; }

			// --- ibBackendTypeConfigFactory — the Type property's variant resolves through this (mirror
			// ibFormAttribute). GetTypeDesc's signature ALSO closes ibBackendSourceColumn::GetTypeDesc (one
			// final overrider). The column carries no metaobject → GetMetaData yields the active config. ------
			virtual ibTypeDescription& GetTypeDesc() const override { return m_propertyType->GetValueAsTypeDesc(); }
			virtual ibSelectorDataType GetFilterDataType() const override { return ibSelectorDataType::ibSelectorDataType_reference; }
			virtual const ibMetaData* GetMetaData() const override;

			// --- ibBackendSourceColumn — SOURCE-COLUMN presentation. The value-table's explorer vends THIS as
			// each column's descriptor, so the bound tablebox resolves the header (GetSynonym = Caption) and
			// type through the same WalkColumns / GetSourceAbstractColumn seam a metadata field uses. GetComment
			// stays the base default (empty) — a column carries no comment. ------------------------------------
			virtual wxString GetName() const override { return m_propertyName->GetValueAsString(); }
			// Header = the Caption; empty Caption falls back to the auto-generated synonym of the Name — the
			// last priority level (Caption > metadata > auto-name), now truly mirroring ibFormAttribute::GetSynonym.
			virtual wxString GetSynonym() const override {
				return !m_propertyCaption->IsEmptyProperty() ? m_propertyCaption->GetValueAsTranslateString() : stringUtils::GenerateSynonym(GetColumnName());
			}

			// --- Property events (fired by the inspector on an edit) — grouped last, per convention. Nothing
			// to sync (the property variants ARE the storage); see the .cpp. ------------------------------------
			virtual void OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue) override;

			// --- Serialization — through the UNIFIED property mechanism (each property's GetNodeValue /
			// ReadNodeValue, exactly like ibFormAttribute), plus the non-property id / width. The value-table
			// creates one child node per column and delegates here. ---------------------------------------------
			virtual bool WriteProperty(ibDataNode& node) const override;
			virtual bool ReadProperty(const ibDataNode& node) override;

		private:

			// SINGLE SOURCE OF TRUTH = the property variants (NOT a parallel member array): every accessor
			// reads / writes THROUGH the property, exactly like ibFormAttribute — an inspector edit and a
			// runtime / explorer read see the same value, nothing to mirror or drift. Only the identity
			// (m_columnID) and the non-editable width are plain members. Members grouped last, per convention.
			unsigned int m_columnID = 0;
			int m_columnWidth = wxDVC_DEFAULT_WIDTH;

			ibPropertyCategory* m_categoryCommon = ibPropertyObject::CreatePropertyCategory(wxT("Common"), _("General"));
			ibPropertyUString* m_propertyName = ibPropertyObject::CreateProperty<ibPropertyUString>(m_categoryCommon, wxT("Name"), _("Name"), _("Column name"), wxT(""));
			ibPropertyTString* m_propertyCaption = ibPropertyObject::CreateProperty<ibPropertyTString>(m_categoryCommon, wxT("Caption"), _("Caption"), _("Column caption (header)"), wxT(""));
			ibPropertyType* m_propertyType = ibPropertyObject::CreateProperty<ibPropertyType>(m_categoryCommon, wxT("Type"), _("Type"), ibValueTypes::TYPE_STRING);

			friend ibValueModelTableColumnCollection;
		};

	public:

		ibValueModelTableColumnCollection(ibValueModelTable* ownerTable = nullptr);
		virtual ~ibValueModelTableColumnCollection();

		ibValueModelColumnInfo* AddColumn(const wxString& colName,
			const ibTypeDescription& typeData,
			const wxString& caption,
			int width = wxDVC_DEFAULT_WIDTH) override {

			unsigned int max_id = 0;

			for (auto& col : m_listColumnInfo) {
				if (max_id < col->GetColumnID()) {
					max_id = col->GetColumnID();
				}
			}

			for (long row = 0; row < m_ownerTable->GetRowCount(); row++) {
				ibComposerNode* node = m_ownerTable->GetViewData<ibComposerNode>(m_ownerTable->GetItem(row));
				wxASSERT(node);
				node->SetValue(max_id + 1, ibValueTypeDescription::AdjustValue(typeData));
			}

			ibValueModelTableColumnInfo* colInfo =
				new ibValueModelTableColumnInfo(max_id + 1, colName, typeData, caption, width);
			// Structural attach-owner (NOT AttachPropertyObject — we don't want the column's props to
			// flatten into the table's inspector): the notify chain column -> value-table -> holder, so a
			// Caption / Name / Type edit bubbles up and the bound control re-renders live.
			colInfo->SetAttachOwner(static_cast<ibPropertyObject*>(m_ownerTable));
			return m_listColumnInfo.emplace_back(colInfo);
		}

		const ibTypeDescription GetColumnType(unsigned int col) const {
			for (auto& colInfo : m_listColumnInfo) {
				if (col == colInfo->GetColumnID()) {
					return colInfo->GetColumnType();
				}
			}
			return ibTypeDescription();
		}

		virtual void RemoveColumn(unsigned int col) {

			for (long row = 0; row < m_ownerTable->GetRowCount(); row++) {
				ibComposerNode* node = m_ownerTable->GetViewData<ibComposerNode>(m_ownerTable->GetItem(row));
				wxASSERT(node);
				node->EraseValue(col);
			}

			auto it = std::find_if(m_listColumnInfo.begin(), m_listColumnInfo.end(),
				[col](ibValueModelTableColumnInfo* colInfo) {
					return col == colInfo->GetColumnID();
				}
			);

			m_listColumnInfo.erase(it);
		}

		virtual ibValueModelColumnInfo* GetColumnInfo(unsigned int idx) const {
			if (idx >= m_listColumnInfo.size())
				return nullptr;
			auto it = m_listColumnInfo.begin();
			std::advance(it, idx);
			return *it;
		}

		virtual unsigned int GetColumnCount() const { return m_listColumnInfo.size(); }

		void FillMembers(ibMemberTable& helper) const;   // bound in ctor (was PrepareNames)

		//WORK AS AN AGGREGATE OBJECT
		virtual bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray);
		virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);

		//array support
		virtual bool SetAt(const ibValue& varKeyValue, const ibValue& varValue);
		virtual bool GetAt(const ibValue& varKeyValue, ibValue& pvarValue);

		friend class ibValueModelTable;

	protected:

		ibValueModelTable* m_ownerTable;
		std::vector<ibValuePtr<ibValueModelTableColumnInfo>> m_listColumnInfo;
	};

	class ibValueModelTableReturnLine : public ibValueModelReturnLine {
	public:

		ibValueModelTableReturnLine(ibValueModelTable* ownerTable = nullptr, const ibDataViewItem& line = ibDataViewItem(nullptr));
		virtual ~ibValueModelTableReturnLine();

		virtual ibValueModel* GetOwnerModel() const { return m_ownerTable; }

		void FillMembers(ibMemberTable& helper) const;   // bound in ctor (was PrepareNames)

		virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal); //setting attribute
		virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal); //attribute value

	private:
		ibValueModelTable* m_ownerTable;
	};

public:

	virtual ibDataViewItem FindRowValue(const ibValue& varValue, const wxString& colName = wxEmptyString) const;

	virtual bool AutoCreateColumn() const { return true; }

	virtual ibValueModelColumnCollection* GetColumnCollection() const { return m_tableColumnCollection; }

	virtual ibValueModelTableReturnLine* GetRowAt(const long& line) {
		if (line > GetRowCount())
			return nullptr;
		return new ibValueModelTableReturnLine(this, GetItem(line));
	}

	virtual ibValueModelReturnLine* GetRowAt(const ibDataViewItem& line) {
		if (!line.IsOk())
			return nullptr;
		return GetRowAt(GetRow(line));
	}

	// ibSourceDataObject hop gate. Set: many rows, no single cell -> no-op. Get: the DESIGN-TIME dot-walk steps
	// by TYPE not value (a value-table has 0..N rows) -> the pinned branch's empty typed twin; out-of-line
	// because it needs reference.h (CoerceHopType). See valueTable.cpp.
	virtual bool SetValueBySourceHop(const ibSourceHop& hop, const ibValue& value) override { return false; }
	virtual bool GetValueBySourceHop(const ibSourceHop& hop, ibValue& out) const override;

	//set meta/get meta
	virtual bool SetValueByMetaID(const ibDataViewItem& item, const ibMetaID& id, const ibValue& varMetaVal) {
		ibComposerNode* node = GetViewData<ibComposerNode>(item);
		if (node == nullptr)
			return false;
		return node->SetValue(id, ibValueTypeDescription::AdjustValue(m_tableColumnCollection->GetColumnType(id), varMetaVal), true);
	}

	virtual bool GetValueByMetaID(const ibDataViewItem& item, const ibMetaID& id, ibValue& pvarMetaVal) const {
		ibComposerNode* node = GetViewData<ibComposerNode>(item);
		if (node == nullptr)
			return false;
		node->GetValue(id, pvarMetaVal);
		// Lazy retype: a column's Type may have changed AFTER a cell was written, so coerce the stored value to
		// the column's CURRENT type ON READ instead of sweeping every row on a type edit. A stale-typed cell is
		// converted; an absent / empty cell yields the typed empty — "nothing there" reads back cleanly.
		pvarMetaVal = ibValueTypeDescription::AdjustValue(m_tableColumnCollection->GetColumnType(id), pvarMetaVal);
		return true;
	}

	ibValueModelTable();
	ibValueModelTable(const ibValueModelTable& val);
	virtual ~ibValueModelTable();

	virtual void AddValue(unsigned int before = 0) {
		long row = StorageIndexOf(GetSelection());   // displayed item is a composer copy → storage index via bridge
		if (row > 0)
			AppendRow(row);
		else AppendRow();
	}

	virtual void CopyValue() { CopyRow(); }
	virtual void EditValue() { EditRow(); }
	virtual void DeleteValue() { DeleteRow(); }

	//array
	virtual bool GetAt(const ibValue& varKeyValue, ibValue& pvarValue);

	//check is empty (single final overrider for BOTH ibValueModel::IsEmpty and ibSourceDataObject::IsEmpty)
	virtual bool IsEmpty() const override { return GetRowCount() == 0; }

	// A table-of-values is fully composer-driven (filter / sort / group live on the RAM composer), so it exposes
	// the whole List-settings affordance — including GROUP, which folds the flat ТЗ into a tree "лёгким движением".
	virtual Features GetFeatures() const override {
		Features f;
		f.flags |= Features::Filters | Features::Sorting | Features::Grouping;
		return f;
	}

	void FillMembers(ibMemberTable& helper) const;   // bound in ctor (was PrepareNames)

	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal); // attribute value
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);       // method call

	// implementation of base class virtuals to define model
	virtual void GetValueByRow(wxVariant& variant,
		const ibDataViewItem& row, unsigned int col) const override;
	virtual bool SetValueByRow(const wxVariant& variant,
		const ibDataViewItem& row, unsigned int col) override;

	// FETCH: the value-table no longer overrides Get*Fetch. The thin wrappers existed ONLY to bypass the (now
	// removed) ibValueModelStorage::BuildVisibleView fetch override; with that gone, the value-table inherits
	// ibValueModel::GetFirstFetch/Next/Prev → RunComposerPage directly (byte-for-byte the same routing the
	// wrappers did). RunComposerPage runs the RAM composer over ibRamValueStorage (the live nodes) in place.

	//support def. methods (in runtime)
	long AppendRow(unsigned int before = 0);
	void CopyRow();
	void EditRow();
	void DeleteRow();

	ibValueModelTable* Clone() { return new ibValueModelTable(*this); }
	unsigned int Count() { return GetRowCount(); }
	void Clear();

#pragma region _tabular_data_
	// --- ibSourceDataObject — the value-table IS a form data SOURCE -----------------------------------------
	// Covariant GenericData* (GenericData derives from CompositeData) is ONE final overrider that closes the
	// pure GetSourceMetaObject on BOTH unrelated bases: ibTabularObject (via ibValueModel) AND ibSourceObject
	// (via ibSourceDataObject). A RAM table carries no metaobject -> nullptr. (Same trick as ibValueDynamicList.)
	virtual const ibValueMetaObjectGenericData* GetSourceMetaObject() const override { return nullptr; }

	//Get ref class
	virtual ibClassID GetSourceClassType() const override { return g_valueTableCLSID; }

	// A RAM table has no metaobject of its own; expose the active config so reference-typed columns still resolve.
	virtual const ibMetaData* GetSourceMetaData() const override;
	virtual wxString GetSourceCaption() const override { return GetClassName(); }

	// The dropped tablebox's column picker reads the columns per-row through this explorer, built from the
	// value-table's OWN column collection (a RAM table has no queryable / metaobject).
	virtual const ibSourceExplorer* GetSourceExplorer() const override;

	// The source shares the value's own refcount. GetGuid returns the guid minted once in the ctor (m_guid)
	// — a stable unique identity per RAM-table instance (no shared null key -> no collisions).
	virtual void SourceIncrRef() override { ibValue::IncrRef(); }
	virtual void SourceDecrRef() override { ibValue::DecrRef(); }
	virtual ibUniqueKey GetGuid() const override;
#pragma endregion

#pragma region _property_object_
	// --- ibPropertyObject + serialization — the columns surface / persist WITH the form attribute. The
	// attribute holder casts the held value to ibPropertyObject and calls Read/WriteProperty, knowing nothing
	// about "a value-table" (same seam as ibValueDynamicList's Source/Settings). -----------------------------
	// GetClassName resolves by the object's own clsid (the factory) and DISAMBIGUATES the two same-name bases
	// (ibValue + ibPropertyObject) — mandatory (mirror ibValueDynamicList).
	virtual wxString GetClassName() const override { return ibValue::GetClassName(); }
	virtual wxString GetObjectTypeName() const override { return GetClassName(); }
	virtual bool IsEditable() const override { return true; }
	virtual void OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue) override;

	// Serialize the column collection: one child node per column (id / name / caption / width + type-desc).
	virtual bool ReadProperty(const ibDataNode& node) override;
	virtual bool WriteProperty(ibDataNode& node) const override;
#pragma endregion

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	// Iterator runtime path lives on ibValueModel — drives Get*Fetch
	// in batches. GetEmptyRow yields the typed skeleton for the
	// IntelliSense type hint that the iterator state surfaces.
	virtual ibValue GetEmptyRow() override {
		return new ibValueModelTableReturnLine(this, ibDataViewItem(nullptr));
	}

	// No source-hook: a RAM model has no queryable. The RAM composer reads ibRamValueStorage (the live
	// nodes) directly — no per-table override / member here.

private:

	ibValuePtr<ibValueModelTableColumnCollection> m_tableColumnCollection;

	// A RAM table has no metaobject / DB identity, so it MINTS its own guid ONCE at construction (an in-class
	// initializer, so BOTH ctors — and a Clone copy — get a fresh, distinct key). GetGuid returns it always,
	// so consumers keying a source by guid never collide on a shared null key.
	ibUniqueKey m_guid = wxNewUniqueGuid;
};

#endif
