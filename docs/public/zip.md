# ZIP archives from a script

**What.** `ZipFileWriter` puts files into a ZIP archive. `ZipFileReader` lists one and takes its files out — from a
file or from its bytes (`BinaryData`). `ZipFileEntry` is one file in it, as the reader lists it. `ZIPStorePathMode` and
`ZIPSubDirProcessingMode` say what of a path the archive keeps and whether a folder's folders go in.

**Why.** Exchange packages, a set of printed forms sent at once, an upload from another system: they come and go as
archives. The format is the engine's own (`core/zip`) — the one a workbook, a backup and the help are packed in.

```
var zip = New ZipFileWriter("C:\out\package.zip");
zip.Add("C:\out\*.pdf");
zip.Add("C:\upload", ZIPStorePathMode.StoreRelativePath, ZIPSubDirProcessingMode.ProcessRecursively);
zip.Write();

var reader = New ZipFileReader(response.GetBodyAsBinaryData());
foreach (item in reader.Items) {
    if (item.Extension == "xml") { reader.Extract(item, "C:\in"); }
}
reader.Close();
```

## Writing

- **Nothing is touched before `Write`.** `New ZipFileWriter(file)` opens nothing; `Add` puts paths on a list. `Write`
  reads each file as it finds it, packs them in the order added and replaces the archive's file whole.
- **`Add` takes a file, a folder or a mask** in the last part (`C:\out\*.pdf`). A folder is its files; with
  `ProcessRecursively` its folders' files too. One `Add` takes its files in the order of their names, so the same folder
  makes the same archive.
- **The name inside:** the name alone (`DontStorePath`, the default), the path below the folder added
  (`StoreRelativePath`), or the whole path without drive and root (`StoreFullPath`). Folders are parted by `/`.
- **A path that names nothing is refused** at `Add`; a mask that matches nothing is not.
- **An archive written into a folder it packs is passed over**, so a second run does not pack the first one's archive.
- Entries are deflated, names are UTF-8, ZIP64 past 4 GB. No password, no encryption.

## Reading

- **Opening reads the archive once** to list `Items` — an `Array` of `ZipFileEntry` in the archive's order, a folder's
  own entry included (its `FullName` ends with `/`). The listing is held, never the contents, and no file is held open:
  an archive of any size opens in the memory of its largest entry.
- **An archive that cannot be read is refused when opened**, where the script names it — not at the first `Extract`.
  An entry that is damaged, encrypted or packed by a method other than stored and deflated is refused, naming it.
  An archive with no entries opens with none.
- **`Extract(item, folder)`** writes one entry — a `ZipFileEntry` or its `FullName` — under its path inside, making the
  folders; `ExtractAll(folder)` writes all of them. A file already there is replaced.
- **`Close()`** lets go of the bytes an archive opened from `BinaryData` holds; `Items` stays readable.
- `ZipFileEntry`: `Name`, `BaseName`, `Extension`, `FullName`, `Path` (the folders before the name, ending with `/`),
  `UncompressedSize`. A name starting with a dot (`.profile`) has no extension.

## What it guarantees

- **An entry lands inside the folder it is extracted to, or nowhere.** A name such as `../../run.bat` or `/etc/passwd`
  is refused, naming the entry — never followed, never quietly shortened. With `ExtractAll`, the entries before it are
  already written.
- **What goes in comes out byte for byte**, any byte and any name.

## Where it stops

- No passwords and no encryption, either way.
- Stored and deflated only; an archive spanned over several files is not read.
- No adding to an existing archive: `Write` replaces it.
