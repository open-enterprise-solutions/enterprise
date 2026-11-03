////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : template object
////////////////////////////////////////////////////////////////////////////

#include "metaTemplateObject.h"
#include "backend/metaData.h"
#include "core/serialize/dataBuilder.h"                    // ibDataNode — per-type node data
#include "backend/backend_composition.h"                   // ibBackendCompositionDescriptionMemory — a composer from before the kinds
#include "backend/propertyManager/property/variant/variantTemplate.h"   // the kind it was, told by the old cell
#include "backend/metaCollection/partial/dataReport.h"     // the owner a composer may have, told about its default composer
#include "backend/system/value/valueSpreadsheet.h"         // what a spreadsheet's code is handed
#include "backend/system/value/valueDataComposition.h"     // …and a composer's; g_valueDataCompositionCLSID — what a composer column holds

//***********************************************************************
//*                              Template base                          *
//***********************************************************************

const ibTemplateProvider& ibValueMetaObjectTemplateBase::GetTemplateProvider() const
{
	return ibTemplateProvider::Get(GetTemplateDesc().GetType());
}

// ⭐ THE DESCRIPTION SERIALISES ITSELF, WHOLE — its kind, and the schema of that kind by the schema's own rules
// (ibTemplateDescriptionMemory). Through its property, so the node's CHILDREN stay the area of metaobjects alone.
//
// ⚠ A COMPOSER FROM BEFORE THE KINDS (MD_CMPS, read as a template — ApplyDataNode) kept its composition under a
// key of its own; a spreadsheet's TemplateData is read as one, a node with no kind being a spreadsheet.
bool ibValueMetaObjectTemplateBase::ReadData(const ibDataNode& node)
{
	if (const auto composition = node.FindProperty(ibT("CompositionData"))) {
		ibCompositionDescription compositionDesc;
		if (!ibBackendCompositionDescriptionMemory::ReadNode(*composition, compositionDesc, m_metaData))
			return false;
		SetTemplateDesc(compositionDesc);
		return true;
	}

	m_propertyTemplate->SetNodeValue(node.GetProperty(m_propertyTemplate->GetName()));
	return true;
}

bool ibValueMetaObjectTemplateBase::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyTemplate->GetName(), m_propertyTemplate->GetNodeValue());
	return true;
}

//***********************************************************************
//*                              Template                               *
//***********************************************************************

ibValueMetaObjectTemplate::ibValueMetaObjectTemplate()
	: ibValueMetaObjectTemplateBase()
{
	// The column face's answer, fixed at construction — a composer holds a composition and nothing
	// else, so nobody downstream has to ask what kind of composer it is.
	m_typeDesc.AppendMetaType(g_valueDataCompositionCLSID);
}

bool ibValueMetaObjectTemplate::OnCreateMetaObject(ibMetaData* metaData, int flags)
{
	return ibValueMetaObjectTemplateBase::OnCreateMetaObject(metaData, flags)
		&& GetTemplateProvider().OnCreateMetaObject(this, flags);
}

bool ibValueMetaObjectTemplate::OnDeleteMetaObject()
{
	return GetTemplateProvider().OnDeleteMetaObject(this)
		&& ibValueMetaObjectTemplateBase::OnDeleteMetaObject();
}

bool ibValueMetaObjectTemplate::OnSaveMetaObject(int flags)
{
	return GetTemplateProvider().OnSaveMetaObject(this, flags)
		&& ibValueMetaObjectTemplateBase::OnSaveMetaObject(flags);
}

bool ibValueMetaObjectTemplate::OnAfterRunMetaObject(int flags)
{
	return GetTemplateProvider().OnAfterRunMetaObject(this, flags)
		&& ibValueMetaObjectTemplateBase::OnAfterRunMetaObject(flags);
}

bool ibValueMetaObjectTemplate::OnReloadMetaObject()
{
	return GetTemplateProvider().OnReloadMetaObject(this)
		&& ibValueMetaObjectTemplateBase::OnReloadMetaObject();
}

// ⭐ THE KIND CHOSEN after the template is made (the designer asks): the provider of the kind it was lets it go as
// if it were deleted, the provider of the kind it is takes it as if it were made with it, and each re-prepares what
// it has a stake in — a composer's report, whose object carries a composition as a field.
void ibValueMetaObjectTemplate::OnPropertyChanged(ibProperty* property, const ibVariant& oldValue, const ibVariant& newValue)
{
	if (property == m_propertyTemplate) {
		const ibVariantTemplate* const was = property->find_cell_variant<ibVariantTemplate>(oldValue);
		const ibTemplateType wasType = was != nullptr ? was->GetTemplateDesc().GetType() : ibTemplateType::None;
		if (wasType != GetTemplateDesc().GetType()) {
			const ibTemplateProvider& wasProvider = ibTemplateProvider::Get(wasType);
			wasProvider.OnDeleteMetaObject(this);
			wasProvider.OnReloadMetaObject(this);
			GetTemplateProvider().OnCreateMetaObject(this, newObjectFlag);
			GetTemplateProvider().OnReloadMetaObject(this);
		}
	}

	ibValueMetaObjectTemplateBase::OnPropertyChanged(property, oldValue, newValue);
}

// ⭐ THE ANSWER TO THE COMPOSITION'S CHANGE SIGNAL: the CONFIGURATION is modified. Which is where
// modified-ness lives (Max, 2026-08-20: "modified-ness is set on the metadata") — a metaobject does not
// carry a dirty flag of its own, the container does, and the designer's Save reads it there.
//
// No payload is needed and none is offered: "something below me changed" is the whole fact. The base
// keeps bubbling, so a template nested under something that also cares stays heard.
void ibValueMetaObjectTemplate::OnChildChanged()
{
	if (m_metaData != nullptr)
		m_metaData->Modify(true);

	ibValueMetaObjectTemplateBase::OnChildChanged();
}

//***********************************************************************
//*                          Common template                            *
//***********************************************************************

ibValueMetaObjectCommonTemplate::ibValueMetaObjectCommonTemplate()
	: ibValueMetaObjectTemplateBase()
{
	SetTemplateDesc(ibTemplateDescription(ibTemplateType::Spreadsheet));
}

//***********************************************************************
//*                    Template provider: spreadsheet                   *
//***********************************************************************

class ibSpreadsheetTemplateProvider : public ibTemplateProvider {
public:

	virtual void CollectContextMenu(ibValueMetaObjectTemplateBase* metaTemplate, std::vector<ibMetaMenuItem>& items) const override {
		items.emplace_back(ibMetaMenuKind::Object, ibT("Template"), _("Open template"), metaTemplate);
	}

	virtual ibValue GetDataValue(ibTemplateDescription& templateDesc, const ibMetaData* metaData) const override {
		ibValueSpreadsheetDocument* valueSpreadsheetDocument = new ibValueSpreadsheetDocument(templateDesc.GetSpreadsheetDesc());
		valueSpreadsheetDocument->InvalidateNames();
		return valueSpreadsheetDocument;
	}
};

//***********************************************************************
//*                     Template provider: composer                     *
//***********************************************************************

// ⭐ UNDER A REPORT A COMPOSER IS ONE OF ITS REPORT'S — the report hears about its composers the way it hears about its
// forms: the FIRST one declared becomes the default, and losing the default one clears it (ibValueMetaObjectReport).
// And a composition is a FIELD of the report's object, so the object is re-prepared when one comes or goes. Any other
// owner has nothing to hear: there a composer is a template code takes.
class ibComposerTemplateProvider : public ibTemplateProvider {
public:

	// One made with its kind (a report over a schema, ibOpenReportOver) is told here; one whose kind is chosen after it
	// is made — the designer asks — when the kind is set (ibValueMetaObjectTemplate::OnPropertyChanged).
	virtual bool OnCreateMetaObject(ibValueMetaObjectTemplate* metaTemplate, int flags) const override {
		ibValueMetaObjectReport* report = dynamic_cast<ibValueMetaObjectReport*>(metaTemplate->GetParent());
		if ((flags & newObjectFlag) != 0 && report != nullptr)
			report->OnCreateComposerObject(metaTemplate);
		return true;
	}

	virtual bool OnDeleteMetaObject(ibValueMetaObjectTemplate* metaTemplate) const override {
		ibValueMetaObjectReport* report = dynamic_cast<ibValueMetaObjectReport*>(metaTemplate->GetParent());
		if (report != nullptr)
			report->OnRemoveComposerObject(metaTemplate);
		return true;
	}

	// ⭐⭐ EVERY VARIANT IS NAMED, AND THE SAVE IS WHERE THAT IS ENFORCED.
	//
	// A variant is how ONE report answers several questions - "sales", "sales with gross margin",
	// "sales by manager" - each a named setting over the same query, picked by the person running it
	// (Max, 2026-09-02). The name is not decoration: it is the whole of what a variant adds to a
	// setting, and it is what the picker shows. An unnamed one is a variant nobody can choose.
	//
	// 🛑 AND IT IS EXACTLY THE THING THAT GETS FORGOTTEN. A composer built verb by verb - query,
	// output, levels, resources - is complete and correct with a nameless variant, and nothing about
	// the result looks wrong until somebody opens the report and finds an empty picker. Max asked for
	// this to be caught rather than remembered: *"make it so it will not let you save without a
	// variant name, so it slaps your hands."*
	//
	// ⚠ REPORTED, NOT THROWN, and for the reason the accounting register documents at length: raising
	// from inside a save leaves the configuration write transaction open, and the next save meets a
	// deadlock naming neither the rule nor the object. A message and a `false` refuse just as firmly
	// and close cleanly.
	virtual bool OnSaveMetaObject(ibValueMetaObjectTemplate* metaTemplate, int flags) const override {

		const ibCompositionDescription& composition = metaTemplate->GetTemplateDesc().GetCompositionDesc();

		for (size_t index = 0; index < composition.m_variants.size(); index++) {

			if (!composition.m_variants[index].m_name.IsEmpty())
				continue;

			// ⭐⭐ INTO THE LEDGER, NOT INTO THE PANE — one road for a refusal, and this is it. The object
			// writes down what is wrong and answers false; whoever ASKED then reads the ledger: the
			// designer puts it in front of the person, a tool puts it in its answer, and the apply dialog
			// greys its button out of it.
			//
			// 🛑 PRINTING IT HERE MADE THE OBJECT SPEAK TO A WINDOW IT KNOWS NOTHING ABOUT, and the cost
			// showed up in the probe: metadata_create and config_check ask "what does this object still
			// lack" by calling OnSaveMetaObject, so a printed complaint stayed on the person's screen as
			// an error that had already been fixed (2026-09-03 - two of these stood in the pane while the
			// configuration saved cleanly).
			//
			// The FIRST one is the report as it opens, so it is worth saying which is meant when a
			// composer has several - counted the way a person would, from one.
			ibValueMetaObject::RestructureError(ibString::Format(
				_("%s: variant %i has no name - a variant IS a named setting, and one without a "
				  "name cannot be picked by anybody running the report"),
				metaTemplate->GetName(), (int)index + 1));

			return false;
		}

		return true;
	}

	// ⭐⭐ AND THE OWNER'S OBJECT LEARNS THAT ITS FIELDS CHANGED — the same road an ATTRIBUTE and a
	// TABULAR SECTION take (ibValueMetaObjectAttributeBase / ibValueMetaObjectTable): a child that adds
	// a field to the object asks the owner to re-prepare, and the owner rebuilds its whole value store.
	//
	// 🛑 THIS IS WHAT WAS MISSING (Max, 2026-08-20 — the assert on a dropped composer). A composition is
	// a FIELD of the report object, created in PrepareEmptyObject beside the attributes; a composer
	// declared AFTER the object was prepared therefore had no field, and the first walk into it met
	// `wxASSERT(it != m_listObjectValue.end())`. In a configuration it was masked — a config object is
	// re-initialised on every CreateObjectValue — and showed itself on an EXTERNAL report, whose object
	// is prepared ONCE when the container is opened.
	virtual bool OnAfterRunMetaObject(ibValueMetaObjectTemplate* metaTemplate, int flags) const override {
		if ((flags & newObjectFlag) != 0 || (flags & pasteObjectFlag) != 0)
			metaTemplate->OnReloadMetaObject();
		return true;
	}

	virtual bool OnReloadMetaObject(ibValueMetaObjectTemplate* metaTemplate) const override {
		ibValueMetaObject* metaObject = metaTemplate->GetParent();
		wxASSERT(metaObject);
		return metaObject->OnReloadMetaObject();
	}

	// "Open" — the settings window that already exists (query / resources / output), not a second editor.
	virtual void CollectContextMenu(ibValueMetaObjectTemplateBase* metaTemplate, std::vector<ibMetaMenuItem>& items) const override {
		items.emplace_back(ibMetaMenuKind::Object, ibT("Composer"), _("Open data composer"), metaTemplate);
	}

	// A COMPOSITION OF ITS OWN — a copy of what the template declares, the way a report's object takes one
	// (ibValueRecordDataObjectReport::PrepareEmptyObject), over the template's own configuration.
	virtual ibValue GetDataValue(ibTemplateDescription& templateDesc, const ibMetaData* metaData) const override {
		return new ibValueDataComposition(templateDesc.GetCompositionDesc(), metaData);
	}
};

//***********************************************************************
//*                          Template provider                          *
//***********************************************************************

const ibTemplateProvider& ibTemplateProvider::Get(ibTemplateType type)
{
	static const ibSpreadsheetTemplateProvider s_spreadsheet;
	static const ibComposerTemplateProvider s_composer;

	if (type == ibTemplateType::Composer)
		return s_composer;
	return s_spreadsheet;
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(ibValueMetaObjectTemplate, "Template", g_metaTemplateCLSID);
METADATA_TYPE_REGISTER(ibValueMetaObjectCommonTemplate, "CommonTemplate", g_metaCommonTemplateCLSID);
