#ifndef __VALUE_LIST_H__
#define __VALUE_LIST_H__

#include "backend/tabularModel.h"
#include "backend/srcDataObject.h"       // ibSourceDataObject — a value list is a form's data source, as a table is
#include "backend/query/queryColumn.h"   // ibBackendSourceColumn — a column of the list describes itself to a form

constexpr ibClassID g_valueListCLSID = value_to_clsid("VL_VLST");
constexpr ibClassID g_valueListItemCLSID = system_to_clsid("VL_VLIT");

// ⭐ A VALUE LIST IS A TABLE OF ONE KIND: its rows are the model's rows, kept where a value table keeps them
// (ibValueModelStorage), and its columns are the kind's own — the value the list is about, and the presentation,
// the check and the picture a row adds on top of it. Nobody declares them and nobody adds to them. So the list
// reads, moves, sorts and walks its rows the way a table does, and a form shows it as one: a table box, the
// current row, the table's commands, the same protocol.
class BACKEND_API ibValueValueList : public ibValueModelStorage, public ibSourceDataObject {
	enum Func {
		enAdd = 0, enInsert, enCount, enGet, enDelete, enClear, enFindByValue,
		enUnloadValues, enLoadValues, enCopy, enSortByValue, enSortByPresentation, enFillChecks, enMove, enIndex,
	};
public:
	// The columns of the kind, by their ids.
	enum Column : unsigned int { eValue = 1, ePresentation, eCheck, ePicture };

	class ibValueValueListColumnCollection : public ibValueModelColumnCollection {
	public:
		class ibValueValueListColumnInfo : public ibValueModelColumnCollection::ibValueModelColumnInfo,
			public ibBackendSourceColumn {
		public:
			ibValueValueListColumnInfo() = default;
			ibValueValueListColumnInfo(unsigned int id, const ibString& name, const ibString& caption, const ibTypeDescription& type) :
				m_columnID(id), m_name(name), m_caption(caption), m_type(type) {}

			virtual unsigned int GetColumnID() const override { return m_columnID; }
			virtual ibString GetColumnName() const override { return m_name; }
			virtual ibString GetColumnCaption() const override { return m_caption; }
			virtual const ibTypeDescription GetColumnType() const override { return m_type; }

			virtual ibString GetName() const override { return m_name; }
			virtual ibString GetSynonym() const override { return m_caption; }
			virtual ibTypeDescription& GetTypeDesc() const override { return m_type; }

		private:
			unsigned int m_columnID = 0;
			ibString m_name, m_caption;
			mutable ibTypeDescription m_type;
		};

		ibValueValueListColumnCollection();
		virtual ibValueModelColumnInfo* GetColumnInfo(unsigned int idx) const override {
			return idx < m_columns.size() ? static_cast<ibValueValueListColumnInfo*>(m_columns[idx]) : nullptr;
		}
		virtual unsigned int GetColumnCount() const override { return static_cast<unsigned int>(m_columns.size()); }

	private:
		std::vector<ibValuePtr<ibValueValueListColumnInfo>> m_columns;
	};

	// An item of the list is the model's row; its properties are the columns.
	class ibValueValueListItem : public ibValueModelReturnLine {
	public:
		ibValueValueListItem(ibValueValueList* ownerList = nullptr, const ibDataViewItem& line = ibDataViewItem(nullptr));
		virtual ibValueModel* GetOwnerModel() const override { return m_ownerList; }
		virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal) override;
		virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal) override;
	private:
		ibValueValueList* m_ownerList;
	};

	ibValueValueList();
	virtual ~ibValueValueList();

	virtual ibValueModelColumnCollection* GetColumnCollection() const override { return m_columns; }
	virtual ibValueModelReturnLine* GetRowAt(const ibDataViewItem& line) override {
		return line.IsOk() ? new ibValueValueListItem(this, line) : nullptr;
	}
	virtual ibValue GetEmptyRow() override { return new ibValueValueListItem(this, ibDataViewItem(nullptr)); }

	virtual bool SetValueByMetaID(const ibDataViewItem& item, const ibMetaID& id, const ibValue& varMetaVal) override;
	virtual bool GetValueByMetaID(const ibDataViewItem& item, const ibMetaID& id, ibValue& pvarMetaVal) const override;
	virtual void GetValueByRow(ibVariant& variant, const ibDataViewItem& row, unsigned int col) const override;
	virtual bool SetValueByRow(const ibVariant& variant, const ibDataViewItem& row, unsigned int col) override;
	virtual void DescribeNewRow(ibNewRowColumns& columns) const override;

	// The table's commands, on the list's rows.
	enum { eAddValue = 1, eDeleteValue = 4, eMoveUpValue = 5, eMoveDownValue, eSortAscValue, eSortDescValue };
	virtual void GetCommandCollection(const ibFormID& formType, std::vector<ibCommandItem>& commands) const override;
	virtual void CallAsCommand(const ibActionID& lNumAction, const ibDataViewCommandContext& ctx, ibBackendValueForm* srcForm) override;
	virtual Features GetFeatures() const override {
		Features f;
		f.flags |= Features::Filters | Features::Sorting;
		return f;
	}

	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray) override;
	// list[index] is the item there, as Get answers.
	virtual bool GetAt(const ibValue& varKeyValue, ibValue& pvarValue) override;
	virtual bool IsEmpty() const override { return GetRowCount() == 0; }

	ibValue Add(const ibValue& value, const ibString& presentation, bool check, const ibValue& picture);
	ibValue Insert(unsigned int index, const ibValue& value, const ibString& presentation, bool check, const ibValue& picture);
	ibValue Get(unsigned int index);
	// An item of this list removes that item, a number is an index; anything else is refused.
	void Delete(const ibValue& item);
	ibValue FindByValue(const ibValue& value);
	ibValue UnloadValues() const;
	void LoadValues(const ibValue& source);
	// A new list with new items holding the same value, presentation, check and picture.
	ibValue Copy() const;
	void FillChecks(bool check);
	// The item (or the item at that index) moves by offset places; a negative offset moves it up.
	void Move(const ibValue& item, long offset);

#pragma region _source_data_
	virtual const ibValueMetaObjectGenericData* GetSourceMetaObject() const override { return nullptr; }
	virtual ibClassID GetSourceClassType() const override { return g_valueListCLSID; }
	virtual const ibMetaData* GetSourceMetaData() const override;
	virtual ibString GetSourceCaption() const override { return GetClassName(); }
	virtual const ibSourceExplorer* GetSourceExplorer() const override;
	virtual void SourceIncrRef() override { ibValue::IncrRef(); }
	virtual void SourceDecrRef() override { ibValue::DecrRef(); }
	virtual const ibUniqueKey& GetGuid() const override { return m_guid; }
#pragma endregion

private:
	void FillMembers(ibMemberTable& helper) const;
	// Where an item of this list sits, or the index a number names. Anything else is refused.
	ibComposerNode* RowOf(const ibValue& item) const;

	ibValuePtr<ibValueValueListColumnCollection> m_columns;
	ibUniqueKey m_guid = wxNewUniqueGuid;
};

#endif
