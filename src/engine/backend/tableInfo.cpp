////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : table model information
////////////////////////////////////////////////////////////////////////////

#include "tableInfo.h"

#include "backend/session/session.h"



ibValueModel::ibValueModel()
	: ibValue(ibValueTypes::TYPE_VALUE),
	m_modelProvider(nullptr)
{
	m_modelProvider = new ibDataViewModelProviderImpl(this);
	//m_modelProvider->IncRef(); // always one
}

ibValueModel::~ibValueModel()
{
	m_modelProvider->DecRef();
}

ibDataViewItem ibValueModel::GetSelection() const
{
	if (m_modelProvider == nullptr)
		return ibDataViewItem(nullptr);
	return m_modelProvider->GetSelection();
}

ibDataViewItem ibValueModel::GetDrillParent() const
{
	if (m_modelProvider == nullptr)
		return ibDataViewItem();
	return m_modelProvider->GetDrillParent();
}

std::future<void> ibValueModel::SubmitFetchAsync(std::function<void()> work)
{
	auto* sess = ibSession::Current();
	if (sess != nullptr)
		return sess->Submit(std::move(work));
	if (work) work();
	std::promise<void> p;
	p.set_value();
	return p.get_future();
}

namespace {
// Cursor-paginated iteration over any flat paged model. Drives the
// model's GetFirstFetch / GetNextFetch surface in fixed-size batches
// and wraps each item via GetRowAt — the same factory the GUI uses to
// turn an ibDataViewItem into a script-visible ReturnLine. RAM-paged
// children pick up filter+sort consistency for free (their Get*Fetch
// slices BuildVisibleView). DB-paged lists become script-iterable
// without any per-class override.
class ibValueModelPagedIteratorState : public ibValueIteratorState {
public:
	explicit ibValueModelPagedIteratorState(ibValueModel* model, int batchSize = 64)
		: m_model(model), m_batchSize(batchSize),
		  m_pos(0), m_started(false), m_exhausted(false) {}

	bool MoveNext(ibValue& current) override {
		if (!m_started) {
			m_started = true;
			m_model->GetFirstFetch(ibDataViewItem(), ibDataViewItem(),
				m_batchSize, m_batch);
			m_pos = 0;
			if (m_batch.size() < static_cast<size_t>(m_batchSize))
				m_exhausted = true;
		} else {
			++m_pos;
			if (m_pos >= m_batch.size()) {
				if (m_exhausted) return false;
				ibDataViewItem anchor = m_batch.empty()
					? ibDataViewItem()
					: m_batch[m_batch.size() - 1];
				ibDataViewItemArray nextBatch;
				m_model->GetNextFetch(ibDataViewItem(), anchor,
					m_batchSize, nextBatch);
				if (nextBatch.empty()) {
					m_exhausted = true;
					m_batch.Clear();
					return false;
				}
				if (nextBatch.size() < static_cast<size_t>(m_batchSize))
					m_exhausted = true;
				m_batch = std::move(nextBatch);
				m_pos = 0;
			}
		}
		if (m_pos >= m_batch.size()) return false;

		auto* line = m_model->GetRowAt(m_batch[m_pos]);
		if (line == nullptr) {
			current = ibValue();
		} else {
			current = ibValue(static_cast<ibValue*>(line));
		}
		return true;
	}

	void Reset() override {
		m_batch.Clear();
		m_pos = 0;
		m_started = false;
		m_exhausted = false;
	}

	bool PeekSample(ibValue& current) const override {
		current = m_model->GetEmptyRow();
		// Default GetEmptyRow returns ibValue() (TYPE_EMPTY).
		// Concrete model children override with a typed ReturnLine
		// wrapped as TYPE_REFFER.
		return current.m_typeClass != ibValueTypes::TYPE_EMPTY;
	}

private:
	ibValueModel* m_model;
	int m_batchSize;
	ibDataViewItemArray m_batch;
	size_t m_pos;
	bool m_started;
	bool m_exhausted;
};

inline bool IsFlatPagedModel(const ibValueModel::Features& f) {
	const uint32_t paged = ibValueModel::Features::RamFetch
		| ibValueModel::Features::DbFetch;
	return (f.flags & paged) != 0
		&& (f.flags & ibValueModel::Features::Tree) == 0;
}
} // namespace

std::shared_ptr<ibValueIteratorState> ibValueModel::CreateIterator()
{
	if (IsFlatPagedModel(GetFeatures()))
		return std::make_shared<ibValueModelPagedIteratorState>(this);
	return ibValue::CreateIterator();
}


void ibValueModel::RowValueStartEdit(const ibDataViewItem& item, unsigned int col)
{
	if (m_modelProvider == nullptr)
		return;
	m_modelProvider->StartEditing(item, col);
}

ibValueModel::ibActionCollection ibValueModel::GetActionCollection(const ibFormID& formType)
{
	ibActionCollection action(this);

	if (UseStandartCommand()) {
		action.AddAction(wxT("Add"), _("Add"), g_picAddCLSID, true, eAddValue);
		action.AddAction(wxT("Copy"), _("Copy"), g_picCopyCLSID, false, eCopyValue);
		action.AddAction(wxT("Edit"), _("Edit"), g_picEditCLSID, false, eEditValue);
		action.AddAction(wxT("Delete"), _("Delete"), g_picDeleteCLSID, false, eDeleteValue);
	}

	if (UseFilter()) {
		if (UseStandartCommand()) action.AddSeparator();
		action.AddAction(wxT("Filter"), _("Filter"), g_picFilterCLSID, false, eFilter);
		action.AddAction(wxT("FilterByColumn"), _("Filter by column"), g_picFilterSetCLSID, false, eFilterByColumn);
		action.AddAction(wxT("FilterClear"), _("Filter clear"), g_picFilterClearCLSID, false, eFilterClear);
	}

	if (UseViewMode()) {
		if (UseStandartCommand() || UseFilter()) action.AddSeparator();
		action.AddAction(wxT("ViewMode"), _("View mode"), g_picHierarchyCLSID, false, eViewMode);
	}

	return action;
}

void ibValueModel::ExecuteAction(const ibActionID& lNumAction, ibBackendValueForm* srcForm)
{
	switch (lNumAction)
	{
	case eAddValue:
		AddValue();
		break;
	case eCopyValue:
		CopyValue();
		break;
	case eEditValue:
		EditValue();
		break;
	case eDeleteValue:
		DeleteValue();
		break;
	case eFilter:
		if (ShowFilter()) {
			RefetchAll();
		}
		break;
	case eFilterByColumn:
	{
		const ibDataViewItem& item = GetSelection();
		if (!item.IsOk())
			break;
		if (m_modelProvider != nullptr) {
			ibValue retValue; GetValueByMetaID(item, m_modelProvider->GetCurrentModelColumn(), retValue);
			m_filterRow.SetFilterByID(m_modelProvider->GetCurrentModelColumn(), retValue);
		}
		RefetchAll();
		break;
	}
	case eFilterClear:
		m_filterRow.ResetFilter();
		RefetchAll();
		break;
	case eViewMode:
		ShowViewMode();
		break;
	}
}

///////////////////////////////////////////////////////////////////////////////////////

bool ibValueModel::ShowFilter()
{
	if (m_modelProvider == nullptr)
		return false;
	return m_modelProvider->ShowFilter(m_filterRow);
}

bool ibValueModel::ShowViewMode()
{
	if (m_modelProvider == nullptr)
		return false;
	return m_modelProvider->ShowViewMode();
}

///////////////////////////////////////////////////////////////////////////////////////

ibValueModel::ibValueModelColumnCollection::ibValueModelColumnInfo::ibValueModelColumnInfo() :
	ibValue(ibValueTypes::TYPE_VALUE, true), m_methodHelper(new ibValueMethodHelper())
{
}

ibValueModel::ibValueModelColumnCollection::ibValueModelColumnInfo::~ibValueModelColumnInfo()
{
	wxDELETE(m_methodHelper);
}

enum Prop {
	enColumnName,
	enColumnTypes,
	enColumnCaption,
	enColumnWidth
};

void ibValueModel::ibValueModelColumnCollection::ibValueModelColumnInfo::PrepareNames() const
{
	m_methodHelper->ClearHelper();

	m_methodHelper->AppendProp(wxT("Name"));
	m_methodHelper->AppendProp(wxT("Types"));
	m_methodHelper->AppendProp(wxT("Caption"));
	m_methodHelper->AppendProp(wxT("Width"));
}

bool ibValueModel::ibValueModelColumnCollection::ibValueModelColumnInfo::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	switch (lPropNum)
	{
	case enColumnName:
		pvarPropVal = GetColumnName();
		return true;
	case enColumnTypes:
		pvarPropVal = ibValue::CreateAndPrepareValueRef<ibValueTypeDescription>(GetColumnType());
		return true;
	case enColumnCaption:
		pvarPropVal = GetColumnCaption();
		return true;
	case enColumnWidth:
		pvarPropVal = GetColumnWidth();
		return true;
	}

	return false;
}

ibValueModel::ibValueModelColumnCollection::ibValueModelColumnInfo* ibValueModel::ibValueModelColumnCollection::GetColumnByID(unsigned int col) const
{
	for (unsigned int idx = 0; idx < GetColumnCount(); idx++) {
		ibValueModelColumnInfo* columnInfo = GetColumnInfo(idx);
		wxASSERT(columnInfo);
		if (col == columnInfo->GetColumnID())
			return columnInfo;
	}

	return nullptr;
}

ibValueModel::ibValueModelColumnCollection::ibValueModelColumnInfo* ibValueModel::ibValueModelColumnCollection::GetColumnByName(const wxString& colName) const
{
	for (unsigned int idx = 0; idx < GetColumnCount(); idx++) {
		ibValueModelColumnInfo* columnInfo = GetColumnInfo(idx);
		wxASSERT(columnInfo);
		if (stringUtils::CompareString(colName, columnInfo->GetColumnName()))
			return columnInfo;
	}

	return nullptr;
}