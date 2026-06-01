# Const-meta refactor

> **Status:** LANDED 2026-05-04. 133 files modified, full solution
> builds clean Debug|Win32, smoke-tested. 13 legitimate `const_cast`s
> remain (documented below); one backdoor (`ibMetaData::Find*` family)
> deferred — tracked in `next-session-metadata-const.md`.

## What

Runtime never holds non-const `ibValueMetaObject*`. All meta-getters,
factories, form-getters, and data-object/manager/view ctors take/return
`const` at the runtime path. Mutation of metadata is a designer-side
operation, expressed at well-defined boundaries.

133 files modified, full solution builds clean Debug/Win32, smoke-tested.

## Why

Before this refactor, runtime code could call `obj->ProcessCommand(...)`,
`metaObject->SetSynonym(...)`, etc. on the same metadata that the designer
held mutably — there was no compile-time barrier. The contract "designer
edits, runtime reads" was a verbal one.

Now the contract is enforced by the type system at every direct meta access.
The remaining holes (Find* family on metaData) are documented and tracked.

## What's `const` now

### Meta-object getters

| Method | Result |
|---|---|
| `ibValueMetaObject::GetMetaObject() const` | `const T*` (where T is the derived type — covariant return) |
| `GetSourceMetaObject() const` | `const ibValueMetaObjectCompositeData*` |
| `GetObjectModule() const` | `const ibValueMetaObjectModule*` (renamed from `GetModuleObject`) |
| `GetManagerModule() const` | `const ibValueMetaObjectCommonModule*` (renamed from `GetModuleManager`) |

### Form / template / source factories

All `Get*Form` and `Create*` factories on meta-objects are now `const`:

```cpp
virtual ibBackendValueForm* GetObjectForm(...) const;
virtual ibBackendValueForm* GetListForm(...) const;
virtual ibBackendValueForm* GetSelectForm(...) const;
virtual ibBackendValueForm* GetFolderForm(...) const;
virtual ibBackendValueForm* GetFolderSelectForm(...) const;
virtual ibBackendValueForm* GetGenericForm(...) const;
virtual ibBackendValueForm* GetRecordForm(...) const;       // information register
virtual ibValueSpreadsheetDocument* GetTemplate(...) const;

virtual ibValueManagerDataObject*  CreateManagerDataObjectValue() const;
virtual ibValueRecordDataObjectExt* CreateObjectExtValue() const;
virtual ibValueRecordDataObject*    CreateRecordDataObjectValue() const;
virtual ibValueRecordDataObjectRef* CreateObjectRefValue(...) const;
virtual ibValueRecordSetObject*     CreateRecordSetObjectValue(...) const;
virtual ibValueRecordSetObject*     CreateRecordSetObjectRegValue(...) const;
virtual ibValueRecordManagerObject* CreateRecordManagerObjectValue(...) const;
virtual ibValueRecordManagerObject* CreateRecordManagerObjectRegValue(...) const;
virtual ibSourceDataObject*         CreateSourceObject(const ibValueMetaObjectFormBase*) const;

ibBackendValueForm* CreateAndBuildForm(...) const;
virtual bool ProcessChoice(...) const;
virtual ibValueReferenceDataObject* FindObjectValue(...) const;
```

### Data / manager / view objects

All hold `const ibValueMetaObjectXxx*` as `m_metaObject`. Constructors take
`const ibValueMetaObjectXxx*`:

- `ibValueRecordDataObjectExt`, `ibValueRecordDataObjectRef`,
  `ibValueRecordDataObjectHierarchyRef`
- `ibValueRecordSetObject`, `ibValueRecordManagerObject`, `ibValueRecordKeyObject`
- `ibValueRecordDataObjectCatalog`, `Document`, `Constant`,
  `ChartOfAccounts`, `ChartOfCharacteristicTypes`, `DataProcessor`, `Report`
- `ibValueRecordSetObject{Accounting,Accumulation,Information}Register`
- `ibValueRecordManagerObjectInformationRegister`
- `ibValueManagerDataObject{Catalog,Document,Constant,ChartOfAccounts,
  ChartOfCharacteristicTypes,DataProcessor,Report,AccountingRegister,
  AccumulationRegister,InformationRegister,Enumeration}`
- `ibValueListDataObject`, `ibValueListDataObjectEnumRef`,
  `ibValueListDataObjectRef`, `ibValueListRegisterObject`
- `ibValueModelTreeDataObject`, `ibValueModelTreeDataObjectFolderRef`
- `ibValueSelectorRecordDataObject`, `ibValueSelectorRegisterDataObject`
- `ibValueReferenceDataObject`

### Runtime parent chain

- `SetParent(const ibRuntimeModuleDataObject*)`
- `GetParent() const → const ibRuntimeRoot*`
- `m_parent` is `const T*`
- `GetRoot() const → const ibRuntimeRoot*` (was non-const, returned via const_cast)

### Helpers

- `ibCompileModule(const ibValueMetaObjectModuleBase*)` — was already const,
  removed dead `const_cast` at one caller (moduleInfo.cpp)
- `srcExplorer.h::AppendSource(...)` — accepts `const T*` for all 3 overloads
- `metaData::FindCommonModule(const ibValueMetaObjectCommonModule*)`

## Designer mutation paths

Designer keeps non-const access through:

1. Direct member field `m_metaData` (never goes through `GetMetaData()`)
2. `ProcessCommand(unsigned int id)` is non-const — the menu/button entry
   point for designer actions like "open module editor", "delete metaobject"
3. Property setters (`SetSynonym`, `SetMetaID`, etc.)
4. Reuse: button handlers in `treeDataProcessor.cpp` /
   `treeDataReport.cpp` call `dataProcessor->ProcessCommand(ID_METATREE_OPEN_MODULE)`
   instead of duplicating `OpenFormMDI(metaObject->GetObjectModule())`

## Remaining const_casts in metaCollection (3, all legitimate)

> **Count caveat (2026-05-28 audit):** the original "13 legitimate
> `const_cast`s remain" claim was the metaCollection-narrow count
> at landing. Process-wide grep across `src/engine/backend/` now
> finds 28 occurrences across 17 files (mostly outside the
> const-meta surface — `propertyManager`, `firebirdHlc`,
> `mysql/engine/`, etc.). The 3 in `metaObject.h::ConvertToValue`
> below are still the only ones touching the const-meta path; the
> "floor of 13" figure is no longer accurate process-wide.

- 3 in `metaObject.h::ConvertToValue` template — canonical `dynamic_cast`
  through `const_cast<ibValueMetaObject*>(this)`. Standard C++ pattern;
  cannot be removed without redesigning the conversion API.

That's the floor reachable without going into `ibMetaData` Find* (see below).

## Removed: metadataConfiguration{XML,JSON}.cpp

The XML/JSON configuration export/import code (added by another contributor)
held 10 `const_cast`s on `GetObjectModule`/`GetManagerModule` returns. It
didn't go through the canonical `Dump/RestoreDataFromBuffer` path, was not
covered by the binary serialization tests, and required cascading
const-cast at every `SetModuleText` call site.

Decision: delete rather than maintain.

Removed:
- `src/engine/backend/metadataConfigurationXML.cpp`
- `src/engine/backend/metadataConfigurationJSON.cpp`
- Method declarations in `metadataConfiguration.h`
  (`Save/LoadConfigToXML`, `Save/LoadConfigToJSON`)
- Designer menu IDs `wxID_DESIGNER_CONFIGURATION_{EXPORT,IMPORT}_{XML,JSON}`
- Designer menu items in `mainFrameDesignerMenu.cpp`
- Event handlers in `mainFrameDesignerEvent.cpp`
- vcxproj / filters entries

Re-introduce later through canonical buffer serialization if the use case
returns.

## Method rename: `GetModuleObject/Manager` → `GetObjectModule/ManagerModule`

`GetModuleManager()` (a meta-object's getter for its Manager Module —
the metadata describing the manager-class script) collided semantically
with runtime `ibValueModuleManager` (the runtime owner of compiled
modules — a completely different concept).

Renamed:

| Was | Now |
|---|---|
| `GetModuleObject()` | `GetObjectModule()` |
| `GetModuleManager()` | `GetManagerModule()` |
| `m_propertyModuleObject` | `m_propertyObjectModule` |
| `m_propertyModuleManager` | `m_propertyManagerModule` |

DB property names (`wxT("ObjectModule")`, `wxT("ManagerModule")`) unchanged.
440 call-site references mass-renamed across 78 files.

## Known backdoor (deferred)

`ibMetaData::FindAnyObjectByFilter<T>() const → T*` (non-const). Same
Item-3 anti-pattern in `ibValueMetaObject::FindObjectByFilter` family
(3 overloads by name/id/guid) and `GetCommonMetaObject() const → T*`.

Runtime can do
`m_metaObject->GetMetaData()->FindXxx<X>(id)->NonConstMethod()` to mutate
metadata through this backdoor.

Plugging requires overload pair (Effective C++ Item 3) on each Find/
FillArray/GetAny method — and on every sibling collection helper like
`GetInterfaceArrayObject() const → vector<X*>` that calls them.

First attempt cascaded to 4700+ compile errors. Reverted; tracked as
follow-up in `next-session-metadata-const.md`.
