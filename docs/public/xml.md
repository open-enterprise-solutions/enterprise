# XML from a script

**What.** `XMLReader` reads an XML document node by node. `XMLWriter` writes one, to a file or to
a string. `XMLNodeType` names the kind of node the reader stands on. `XMLNamespaceContext` is the
set of namespaces in scope at one node; a reader or a writer hands it out, a script does not create it.

**Why.** Reports to authorities, bank statements, price lists and exchange with older systems are
XML. The parser is built into the engine (expat), the same on every platform.

```
var reader = New XMLReader();
reader.OpenFile(path);
while (reader.Read()) {
    if (reader.NodeType = XMLNodeType.StartElement And reader.Name = "Price") { reader.Read(); Message(reader.Value); }
}
reader.Close();

var writer = New XMLWriter();
writer.OpenFile(path, "windows-1251");
writer.WriteXMLDeclaration();
writer.WriteStartElement("Order");
writer.WriteAttribute("number", "42");
writer.WriteStartElement("Customer"); writer.WriteText(name); writer.WriteEndElement();
writer.WriteEndElement();
writer.Close();
```

## What reading gives

- **An element is a pair of nodes**, `StartElement` and `EndElement`, written as `<a/>` or as
  `<a></a>`. `IsEmptyElement` tells the two apart.
- **Text, CDATA, a comment and a processing instruction are nodes of their own.** CDATA is never
  merged into the text beside it.
- **The XML declaration is not a node.** `XMLVersion`, `XMLEncoding` and `Standalone` read it.
- **Whitespace between elements** is dropped while `IgnoreWhitespace` is `True` (the default).
  `False` hands it over as `Whitespace` nodes.
- **Attributes** are the current element's. Read them by number (from 0) or by name
  (`AttributeValue`, `GetAttribute`), or walk them with a cursor: `FirstAttribute`, `NextAttribute`
  and `ReadAttribute` stand the reader on one, `MoveToElement` goes back. An attribute that is not
  there is `Undefined`. The `xmlns` declarations are attributes too.
- **Where a node is**: `Depth`, `LineNumber`, `LinePosition`. `Skip()` stops on the end of what it
  skipped. `MoveToContent()` passes comments, instructions and whitespace to the next element, end
  or text, and stays where it is when it already stands on one.

## Encodings

- **A file read** is in the encoding its byte-order mark or its declaration names, UTF-8 when it
  has neither: UTF-8, UTF-16, windows-1251, cp866, ISO-8859-1, US-ASCII.
- **A file written**: `OpenFile(path, encoding = "UTF-8", writeBOM = True)` in UTF-8, UTF-16,
  windows-1251 or cp866. UTF-8 gets a byte-order mark unless told otherwise, UTF-16 always has one,
  windows-1251 and cp866 have none.
- **A string**: the writer's `SetString(encoding)` writes into one and `Close()` returns it; the
  encoding is then only the word in the declaration. The reader's `SetString(text)` reads text that
  is characters already, so an `encoding=` in its declaration is not applied again.
- **A character the encoding has no byte for** is written as a `&#N;` reference in text and in an
  attribute, and read back as the character. In a name, a comment, an instruction or a CDATA
  section it is refused.

## What it guarantees

- **The writer keeps the document well-formed.** An attribute comes only right after its element's
  start, an end only for what was started, and `Close()` refuses while anything is still open.
- **A file that could not be written is raised at `Close()`**, where a full disk shows. A writer
  dropped without `Close()` still writes what it holds to its file; a failure then goes to the
  technology journal.
- **Text that is not XML is refused**, and the error says the line and the column.
- **Nothing outside the document is read.** An external entity, an external DTD or a parameter
  entity is refused: it is not read, and it is not left out quietly.

## Where it stops

- One text node is held whole in memory; the rest of a file is read a piece at a time.
- The `xmlns` declarations come first among the attributes, not where they stood.
- Inside an entity's replacement text `IsEmptyElement` cannot tell `<a/>` from `<a></a>`.
- There is no schema validation (XSD), no XPath and no document tree: a script reads and writes in
  document order.
