# JSON from a script

**What.** `JSONReader` turns JSON text into platform values. `JSONWriter` turns values into JSON
text. Two enumerations name what they deal in: `JSONValueType` (the kind of token the reader stands
on) and `JSONFormatting` (`Compact` or `Indented`). The globals `ReadJSON(reader)` and
`WriteJSON(writer, value)` are the same two verbs.

**Why.** Most web services speak JSON: banks, marketplaces, delivery services, other applications.
JSON is text, so it pairs with anything that carries text. An HTTP body
([http-client.md](http-client.md)) is one example; a file read with `TextReader` is another.

```
var reader = New JSONReader(response.GetBodyAsString());
var order = reader.ReadValue();
Message(order.number + ": " + order.lines.Count());

var writer = New JSONWriter(JSONFormatting.Indented);
writer.WriteValue(order);
request.SetBodyFromString(writer.Close());
```

## What reading gives

| JSON | Platform value |
|---|---|
| object | `Structure`, members in the order written. Read `s.name`, or `s["odd key"]` when a key is not a name |
| array | `Array` |
| number | `Number`, **exact**: read from its text, never through a floating-point double |
| string | `String` (`\u` escapes and surrogate pairs decoded) |
| `true` / `false` | `Boolean` |
| `null` | `Null` |

Two ways to read:
- **Whole**: `ReadValue()` on a fresh reader returns the whole document.
- **Token by token**: `Read()`, `CurrentValueType`, `CurrentValue`, `Skip()`. `ReadValue()` in the
  middle of a walk takes the one value the reader stands on, or the value that follows a property name.

## What it guarantees

- **Numbers are exact.** `0.1 + 0.2` read from JSON is `0.3`, and a 30-digit integer arrives whole.
  A comma decimal locale on the machine does not change what is read.
- **Nothing is merged silently.** If two keys differ only in letter case, `ReadValue()` refuses the
  object, because a `Structure` ignores case and would keep only one. The object can still be read
  token by token. A key that appears twice keeps its last value.
- **Text that is not JSON is refused**, and the error says the line and the column.
- **The writer keeps the document well-formed.** A value inside an object needs its name first, an
  end closes what is open, and `Close()` refuses while anything is still open. `WriteValue` is all
  or nothing: a value that cannot be written leaves nothing half-written behind.
- **What has no JSON form is refused, not guessed**: a reference, a value table, an enumeration
  member. What stands for it in the other system (a code, a name, a Guid) is decided by the
  exchange.
- **Dates go out as ISO 8601** (`2026-09-26T03:04:05`). An empty date goes out as `null`. Nothing is
  read back as a date: JSON has no date type, and which strings are dates is the exchange's
  knowledge.
- **After a dot, `Null`, `True`, `False` and `Undefined` are member names.** So `JSONValueType.Null`
  compiles and `s.null` reads a key named `null`. Everywhere else they are the constants they
  always were.

## Where it stops

- An exponent beyond 1000, or a number token longer than 1000 characters, is refused. Either one
  would cost seconds of arithmetic that nobody asked for.
- Values nested deeper than 256 levels are refused when a value is built or written. Walking token
  by token takes any depth.
- An object whose keys differ only in case cannot be read whole (see above).
