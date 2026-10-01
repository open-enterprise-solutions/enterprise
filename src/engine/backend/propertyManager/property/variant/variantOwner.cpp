#include "variantOwner.h"
#include "backend/metaData.h"

// ⭐⭐ THE WHOLE TREE, not the top level alone. What this variant holds is a list of metaIDs, and they
// do not all name an object that stands at the top: a chart's ACCOUNTING KIND lives inside the chart.
// Searched shallowly it resolves to nothing, and nothing is skipped here without a word — so the
// inspector drew an EMPTY value for a property that was set, and a script reading it got an array with
// a hole in it (Max saw the blank cell, 2026-09-16). An id is unique across the tree, so the depth
// costs the answer nothing; it only decides whether there is one. The writing side searches the same
// way (ibMetaDescriptionMemory::WriteNode), or a binding that was written is unreadable.
wxString ibVariantDataOwner::MakeString() const
{
    const ibMetaData* metaData = m_ownerProperty->GetMetaData();
    if (metaData == nullptr)
        return wxEmptyString;
    wxString strDescr;
    for (unsigned int idx = 0; idx < m_metaDesc.GetTypeCount(); idx++) {
        const ibValueMetaObject* record = metaData->FindAnyObjectByFilter(m_metaDesc.GetByIdx(idx), /*use_child_filter*/ true);
        if (record == nullptr || !record->IsAllowed())
            continue;
        if (strDescr.IsEmpty()) {
            strDescr = record->GetName();
        }
        else {
            strDescr = strDescr + wxT(", ") + record->GetName();
        }
    }
    return strDescr;
}

//////////////////////////////////////////////

#include "backend/system/value/valueArray.h"

ibValue ibVariantDataOwner::GetDataValue() const
{
    ibValueArray* valueArr = new ibValueArray();
    const ibMetaData* metaData = m_ownerProperty->GetMetaData();
    if (metaData != nullptr) {
        for (unsigned int idx = 0; idx < m_metaDesc.GetTypeCount(); idx++) {
            valueArr->Add(metaData->FindAnyObjectByFilter(m_metaDesc.GetByIdx(idx), /*use_child_filter*/ true));
        }
    }
    return valueArr;
}

//////////////////////////////////////////////
