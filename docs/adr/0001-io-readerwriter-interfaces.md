# ADR 0001 — `ibIOReader` / `ibIOWriter` text-format serialization interfaces

- Status: **Accepted**
- Date: 2026-05-19
- Supersedes: the deleted `ibMetaDataConfigurationBase::SaveConfigTo{XML,JSON}` / `LoadConfigFrom{XML,JSON}` methods (commit `ac40a142`, "const-meta refactor", removed 1763 + 1676 lines without replacement)

## Context

OES native configuration storage is a chunked binary `.conf` blob produced by
`ibReaderMemory` / `ibWriterMemory`. The blob is round-trippable but opaque:
no diff-friendly form, no third-party consumer can author it, no schema
exists. Three downstream needs converge on the same gap:

1. **Git-native config** (`ADR-0034 OES Forge`, planned). One file per metadata
   object, human-diffable, mergeable. Binary blobs defeat this.
2. **Pugi `apps/oes-importer`** (commercial AI dev-team SaaS, namespace
   `pugi.io`). Pugi migrates BAS Бухгалтерія 2.1 UA configurations into OES.
   The LGPL/commercial boundary forbids Pugi linking against OES backend
   to call `MetaObject::Save()` directly; Pugi writes filesystem artifacts,
   OES reads them.
3. **AI-generated configurations**. Whether the generator is Codeforge,
   Pugi, or a future tool, the artifact must be a text format with a
   published schema, not a binary blob.

A prior attempt (`metadataConfigurationXML.cpp` + `metadataConfigurationJSON.cpp`,
1763 + 1676 lines) existed at `ac40a142^`. It was deleted whole during a
const-meta refactor with no replacement. The format markers — `OES-XML-2.0`
and `OES-JSON-1.0` — were string literals at the top of the writers; the
readers ignored them. No XSD, no round-trip test, no daemon endpoint. The
old code is recoverable via `git show ac40a142^:…` and serves as a design
baseline, not a copy target.

## Decision

Introduce a formal serialization interface pair in
`src/engine/backend/io/`:

```cpp
class ibIOReader {
public:
    virtual ~ibIOReader() = default;
    virtual bool LoadMetaObject(const wxString& path, ibValueMetaObject& obj) = 0;
    virtual bool LoadMetaObject(wxInputStream& stream, ibValueMetaObject& obj) = 0;
    virtual bool LoadForm(const wxString& path, ibValueMetaObjectFormBase& form) = 0;
    virtual bool LoadForm(wxInputStream& stream, ibValueMetaObjectFormBase& form) = 0;
    virtual bool LoadTableDoc(const wxString& path, ibSpreadsheetDescription& doc) = 0;
    virtual bool LoadTableDoc(wxInputStream& stream, ibSpreadsheetDescription& doc) = 0;
    virtual wxString FormatId() const = 0;     // e.g. "oes-xml-1.0"
    virtual wxString LastError() const = 0;
};

class ibIOWriter {
public:
    virtual ~ibIOWriter() = default;
    virtual bool SaveMetaObject(const ibValueMetaObject& obj, const wxString& path) = 0;
    virtual bool SaveMetaObject(const ibValueMetaObject& obj, wxOutputStream& stream) = 0;
    virtual bool SaveForm(const ibValueMetaObjectFormBase& form, const wxString& path) = 0;
    virtual bool SaveForm(const ibValueMetaObjectFormBase& form, wxOutputStream& stream) = 0;
    virtual bool SaveTableDoc(const ibSpreadsheetDescription& doc, const wxString& path) = 0;
    virtual bool SaveTableDoc(const ibSpreadsheetDescription& doc, wxOutputStream& stream) = 0;
    virtual wxString FormatId() const = 0;
    virtual wxString LastError() const = 0;
};
```

Both the path-overload and the stream-overload exist on every entry so that:

- daemon endpoints can hand `IReader` a `wxMemoryInputStream` over an HTTP
  body without first writing to disk
- importers can hand `IWriter` a `wxFileOutputStream` and let the implementation
  own the open/close

Concrete implementations:

| Class            | Header                            | Format id     | Library                  |
|------------------|-----------------------------------|---------------|--------------------------|
| `ibXmlReader`    | `src/engine/backend/io/xmlReader.h`  | `oes-xml-1.0` | `wxXmlDocument`          |
| `ibXmlWriter`    | `src/engine/backend/io/xmlWriter.h`  | `oes-xml-1.0` | `wxXmlDocument`          |
| `ibJsonReader`   | `src/engine/backend/io/jsonReader.h` | `oes-json-1.0`| `nlohmann/json` v3.11.3  |
| `ibJsonWriter`   | `src/engine/backend/io/jsonWriter.h` | `oes-json-1.0`| `nlohmann/json` v3.11.3  |

Versioning: the `format` + `version` fields at the root of every artifact
become **mandatory** on read. A loader rejects unknown formats with a
populated `LastError()`. Future format bumps go through a migration path
inside the concrete class, not by silently accepting new shapes.

XSD published at `data/schemas/oes-{metaobject,form,tabledoc}.xsd`. Sample
round-trip artifacts at `samples/io-roundtrip/`. Round-trip is enforced
by a unit test in `tests/test_ioRoundTrip.cpp`: serialize → write → read →
compare structural equality (`MetaObject == MetaObject` field-by-field).

## Rejected alternatives

- **libxml2.** Heavier, separate dep, separate license review, no integration
  with `wxFileInputStream`. `wxXmlDocument` is already linked and used
  everywhere in the codebase (`metaDataConfiguration.cpp`, every form
  loader). Reject.
- **RapidJSON / jsoncpp.** `nlohmann/json` v3.11.3 is already vendored at
  `src/3rdparty/nlohmann/json.hpp` and used by the web frontend. Reject
  the alternatives on no-new-deps grounds.
- **Keep binary `.conf` as the canonical format, treat XML/JSON as
  read-only export.** Defeats Pugi's import path. The daemon needs a
  symmetric reader. Reject.
- **One `ibIO` interface covering both directions.** Conflates reader and
  writer ownership lifetimes (a writer may need exclusive output access
  guarantees that a reader doesn't). Mixing them complicates future
  read-only / write-only specialisations. Reject.
- **Drop the binary `.conf` format.** Out of scope for this ADR. The
  binary format remains the primary on-disk representation for live
  databases. Text formats are for git storage, migration, and AI
  authoring.

## Consequences

Positive:
- Pugi can write XML conforming to published XSD without ever linking
  against OES. The LGPL boundary stays clean.
- Configurations become diff-friendly. Git-native storage (`ADR-0034`)
  unblocks.
- AI generators have a documented target format.
- Round-trip is enforceable via test, not aspirational.

Negative:
- Five new files under `src/engine/backend/io/` plus a CMake target.
- Forms and TableDocs need brand-new XML/JSON paths — neither has a
  prior text serialization, so the chunked binary topology
  (`ibReaderMemory`/`ibWriterMemory` + `LoadControl`/`SaveControl` +
  `ibSpreadsheetDescriptionMemory`) must be mapped element-by-element.
  Expect mistakes on the first iteration; the round-trip test catches
  them.
- Designer menu items wire to the new API; the old four menu commands
  are gone (`ac40a142`), the new four hang off `ibIOWriter`/`ibIOReader`
  via a format dropdown.

## Open questions (tracked in follow-up ADRs)

- **OQ-1.** Backward-compat shim for binary `.conf` files alongside
  XML/JSON. Current decision: keep `.conf` as the on-disk format for
  databases, XML/JSON as the export/import path. No deprecation path
  needed because `.conf` stays primary.
- **OQ-2.** BAS native form XML → OES form XML conversion lives on the
  Pugi side (`apps/oes-importer/transforms/`), not in OES. OES reads
  only `oes-xml-1.0`.
- **OQ-3.** Schema-evolution policy. v1.1 adds optional fields; v2.0
  may break. Loaders pin to a major version; an explicit
  `MigrateXMLv1to2` helper lives next to the reader when needed.
