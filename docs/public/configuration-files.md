# Configuration files

A configuration can be kept in a source repository apart from its database: metadata, modules, forms, report
compositions, spreadsheet layouts and binary assets. No records, user accounts or passwords. Keep the database
itself outside version control.

## Saving and loading

- **Designer:** *Configuration → Save configuration to files…* / *Load configuration from files…*. The directory
  is on the Designer's own computer; the files travel to the server and back as one packet.
- **MCP:** `config_export_files` / `config_import_files` — the directory is on the server's host.

```json
{"name":"config_export_files","arguments":{"directory":"/path/to/repository/configuration"}}
{"name":"config_import_files","arguments":{"directory":"/path/to/repository/configuration","confirm":true}}
```

Loading replaces the edited configuration; it does not save or apply the database. Check it, save it and review
the database differences before applying (`config_check`, `database_diff`, `config_save`, `config_apply`).

## Layout

```
configuration.json                  the configuration itself
Catalog/Goods/node.json             a part: its type's folder, its name's under it
Catalog/Goods/Attribute/Price/…     its parts, in folders under it
CommonModule/Main/Module.txt        a text of several lines, beside its node
Document/Receipt/ObjectModule.Module.txt   …at any depth of a value: the names on the way, a dot between
CommonTemplate/Logo/Template.bin    a binary, beside its node
```

- A node file is the node in the engine's JSON form: `NodeType` (by name when the name reads back as the same
  type, else by number), `NodeId`, the fields in their order, `NodeRaw` for a transitional raw block.
- A field kept in a file is `{"NodeFile": "Module.txt"}`; a node's children are `NodeFolders`, the list of their
  folders in order.
- A part is two folders: its type (as the registry names it — `Catalog`, `CommonModule`), then its `Name`; a part
  with no name by its GUID, or by its place (`item-N`).
- A file beside a node is named by its field — inside a sub-node or a list, by the names on the way
  (`ObjectModule.Module.txt`, `Forms.0.txt`); where a name cannot be a file name, or is repeated, by the places
  on the way (`field-N`, `field-N.M`).
- Names are compared without case: two parts that differ only by case refuse, rather than becoming one folder on
  Windows or macOS.
- The metadata ids stay in the content (`NodeId`): references use them, and loading keeps them.
- UTF-8 throughout. Saving an unchanged configuration again gives the same paths and bytes.
- A date is its ISO text (milliseconds when there are any), read back as a date by whoever expects one.

## Publication and refusal

- Choose a directory of its own **inside** the repository — not the repository root, not the database directory.
  A directory that holds anything but an earlier export (a `.git`, a stray file) is refused, and left alone.
- The new export is written into a sibling staging directory, the old one moved aside, the new one renamed into
  place; a failure puts the old one back. A crash between the renames can leave the old export at
  `.stage-<GUID>.previous` — rename it back. Do not save to one directory from two places at once.
- Removed parts disappear from the directory on the next save.
- Loading reads every file the export names before anything changes, and refuses: an unknown format version, a
  missing file, a name given twice, a path out of the directory, a symbolic link, too deep a nesting, a type not
  known to this build. A refused load leaves the edited configuration as it was.
