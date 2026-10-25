# Configuration files

A configuration can be stored in a source repository independently of its database.
The export contains metadata, modules, forms, report compositions, spreadsheet
layouts and binary assets. It contains no application records, user accounts or
passwords. Keep the local database outside version control.

In the desktop Designer, use **Configuration > Export configuration to files...**
and choose a dedicated subdirectory of the repository, for example `configuration`.
To restore it, open a fresh database in the Designer and use **Configuration >
Import configuration from files...**. Import replaces the edited configuration;
check it, save it and review the database differences before applying it.

The same operations are available over the existing MCP JSON-RPC protocol:

```json
{"name":"config_export_files","arguments":{"directory":"/path/to/repository/configuration"}}
{"name":"config_import_files","arguments":{"directory":"/path/to/repository/configuration","confirm":true}}
```

The directory belongs to the **Designer host**, including when the MCP client is on
another machine. Import requires explicit confirmation. It loads metadata into the
editor; it does not save or apply the database. Use `config_check`, `database_diff`,
`config_save` and `config_apply` to review and publish the result. The directory
operations are available in the desktop Designer and its MCP endpoint; the thin
Designer's upload/download commands still transfer the existing configuration file.

## Layout and types

`configuration.json` declares `format: "OES-Configuration"`, `version: 1`, and the
root node. Each metadata child has a file at
`objects/<GUID>/node.json`, with descendants below their parent's GUID directory.
The configuration root occupies the manifest. Structured children without a metadata
GUID use their ordinal position. An object's name or process-local node id is never
used as its file name. Numeric metadata ids remain in the content because existing
metadata references use them; import preserves them rather than allocating new ids.

Every node declares a numeric `class`, an integer `id`, and separate ordered
`fields`, `properties` and `children` arrays. Named entries are `[name, value]`
pairs, preserving order and repeated names. A value declares its `kind`:

- `empty`, `bool` and `string` retain their original types, including empty strings
  and embedded NUL characters.
- `number` stores the exact decimal as text, avoiding floating-point rounding.
- `date` stores the signed millisecond count of `ibDateTime`, without conversion to
  an instant or the machine's time zone.
- `child` holds a nested node or null; `array` holds an ordered list of typed values.
- `binary` names a separate `.bin` file. A node's transitional raw block is also a
  separate file. Text containing line breaks lives in a UTF-8 `.txt` file so module
  edits have ordinary line diffs. Other strings stay in the node's JSON.

Asset references are relative to the export directory, not to the referencing node.
All text is UTF-8. Output has stable indentation, LF line endings and no timestamps.
Exporting an unchanged imported configuration produces the same paths and bytes.

Some metadata payloads still arrive at the provider as binary: interface links,
role permissions, composition membership and functional-option membership are
written that way by `ibValueMetaObject::SaveNode`. The provider preserves them
faithfully as sidecar files; it does not reinterpret their schema. Pictures and
other genuine binary assets use the same representation. Forms, compositions and
spreadsheet descriptions follow their existing structured node serialization.

## Publication and refusal

Use a dedicated export directory **inside** a repository; do not select the
repository root or the database directory. An existing export is read and validated
before replacement. Files not referenced by its manifest, including nested files
or a `.git` directory, prevent replacement. Deleted objects and assets disappear
on the next successful export.

Export writes a complete sibling staging directory, moves the old export to a
backup, and renames the staging directory into place. A failed publish attempts to
restore the backup. These are directory renames on one filesystem, not a transaction
with Git or the database. A process or power failure between the renames can leave
the previous export at the sibling `.stage-<GUID>.previous` path; recover it by
renaming that directory back. Do not run concurrent exports to the same destination.
An undeletable backup is retained after successful publication.

Read rejects unknown format versions, missing files, invalid value types, repeated
file references, cycles, excessive nesting, absolute paths, parent traversal and
symbolic links. The provider builds a fresh tree and only assigns the result after
all referenced files have been read. Configuration import then enters the existing
configuration-file load path, which applies metadata to a detached root before
replacing the edited tree. A corrupt export therefore does not clear the existing
metadata. Existing configuration-file lifecycle and validation rules still apply.

`ibFileProvider` mirrors `ibJsonProvider` as an `ibFormatProvider`, with lossless
storage semantics. Its memory overloads read/write the root manifest and resolve
sidecar files under the supplied directory. Its directory overloads read
`configuration.json` and publish a complete export. The configuration's
`SaveConfigToFiles`/`LoadConfigFromFiles` mirror `SaveConfigToFile`/`LoadConfigFromFile`;
loading enters the existing binary container door so there is one metadata loading
and lifecycle mechanism.
