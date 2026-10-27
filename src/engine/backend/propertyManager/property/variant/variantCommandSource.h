#ifndef __COMMAND_SOURCE_DATA_VARIANT_H__
#define __COMMAND_SOURCE_DATA_VARIANT_H__

#include "core/variant.h"
#include "backend/propertyManager/property/variant/variantType.h"
#include "core/description/commandDescription.h"
#include "backend/backend_command.h"   // ibBackendCommandReceiver — the factory the variant WALKS to validate the binding

// The variant behind a command-source property — the command-door twin of ibVariantSource. Where the
// source variant holds an ibSourceDescription (the address of a VALUE inside a data source), this holds an
// ibCommandDescription (the address of a COMMAND: a hop path own / main / table / general). It is the ONLY
// stored state — serialised by the property and fed to the button, exactly like the source variant's path.
class BACKEND_API ibVariantCommandSource : public ibVariant {
public:

	bool IsEmptyCommand() const { return !m_commandDesc.IsOk(); }

	// The button / bar item the binding is walked on — what its commands are asked of.
	const ibBackendCommandReceiver* GetOwner() const { return m_owner; }

	// The binding address — a command-hop path (own object / main attribute / table / general command). The
	// variant's only stored state; the property serialises THIS (mirrors ibVariantSource::GetSourceDesc).
	ibCommandDescription& GetCommandDesc() { return m_commandDesc; }
	const ibCommandDescription& GetCommandDesc() const { return m_commandDesc; }
	void SetCommandDesc(const ibCommandDescription& desc) { m_commandDesc = desc; }

	// The display name (set by the picker when a command is chosen) — kept so the inspector cell reads it back.
	const ibString& GetDisplay() const { return m_display; }
	void SetDisplay(const ibString& display) { m_display = display; }

	ibVariantCommandSource() : ibVariant(ibValueTypes::TYPE_VALUE) {}
	ibVariantCommandSource(const ibBackendCommandReceiver* owner, const ibCommandDescription& desc, const ibString& display = ibEmptyString)
		: ibVariant(ibValueTypes::TYPE_VALUE), m_owner(owner), m_commandDesc(desc), m_display(display) {}
	ibVariantCommandSource(const ibVariantCommandSource& src)
		: ibVariant(ibValueTypes::TYPE_VALUE), m_owner(src.m_owner), m_commandDesc(src.m_commandDesc), m_display(src.m_display) {}

	virtual bool CompareValueEQ(const ibVariant& data) const override {
		const ibVariantCommandSource* cmdData = dynamic_cast<const ibVariantCommandSource*>(data.GetRef());
		return cmdData != nullptr && m_commandDesc == cmdData->m_commandDesc;   // full identity (path AND form name)
	}

	// The string the cell shows — SERVER-SIDE and live, exactly like ibVariantSource::GetString asks its source
	// factory. The owner (a button / a bar item) is cast to ibBackendCommandReceiver; WalkCommand WALKS the bound hops
	// and returns valid / invalid. Can't walk to the end (a hop gone) -> "<not found>", the guarantee the path broke;
	// this runs at RUNTIME too, so a deleted command reads "<not found>" everywhere, not just in the designer cell.
	// Only with no owner wired (a detached temp) does it fall back to the picker-set display.
	virtual ibString GetString() const override {
		if (!m_commandDesc.IsOk())
			return ibT("<not selected>");                        // nothing bound
		ibString text;
		if (m_owner != nullptr)
			return m_owner->WalkCommand(m_commandDesc, &text) ? text : ibString(ibT("<not found>"));
		return m_display.IsEmpty() ? ibString(ibT("<not selected>")) : m_display;   // no owner to walk -> cached picker name
	}

protected:

	const ibBackendCommandReceiver* m_owner = nullptr;   // the button / bar item — WALKED to validate the binding (existence + name)
	ibCommandDescription m_commandDesc;   // binding address: the command-hop path
	ibString             m_display;       // picker-set display name (fallback only when no owner is wired)
};

#endif // !__COMMAND_SOURCE_DATA_VARIANT_H__
