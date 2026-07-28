# Pictures — three kinds, one description

> **Scope:** how images are stored, referenced and resolved — `backend/backend_picture.*`,
> `pictureDescription.*`, `picturePredefined.*`, `metaCollection/metaPictureObject.*`,
> `propertyManager/property/propertyPicture*`.
> Companions: [property-system.md](property-system.md),
> [script-value-types.md](script-value-types.md) (`StoragePicture`),
> [command-interface.md](command-interface.md) (commands carry pictures).
> This is foundation code.

---

## 1. The idea — a picture is a *reference*, not bytes

Almost nothing in the engine holds an image. It holds an **`ibPictureDescription`**: a
small tagged value saying *where the picture comes from*. Bytes are resolved only when
something must actually draw.

```cpp
enum ibPictureType
{
    eFromBackend = 1,     // built into the engine  — addressed by ibPictureID (a clsid)
    eFromConfiguration,   // a Picture metaobject   — addressed by ibGuid
    eFromFile,            // external image         — the bytes travel with it
};
```

Those are the three kinds. Everything below follows from them.

---

## 2. `ibPictureDescription` — the tagged value

```cpp
struct ibPictureDescription {
    ibPictureType m_type;

    ibPictureDescription()                                   : m_type(eFromBackend) {}
    ibPictureDescription(const ibPictureID& id)              : m_type(eFromBackend),       m_class_identifier(id) {}
    ibPictureDescription(const ibGuid& id)                   : m_type(eFromConfiguration), m_meta_guid(id) {}
    ibPictureDescription(const ibExternalPictureDescription& data) : m_type(eFromFile),    m_img_data(data) {}

    bool IsEmptyPicture() const;
    bool operator ==(const ibPictureDescription& rhs) const;
};
```

**The kind is chosen by the constructor's argument type.** Pass an `ibPictureID` → engine
picture; a `ibGuid` → configuration picture; an `ibExternalPictureDescription` → file
picture. A caller cannot pick the wrong tag for its payload — the type system does it.

`IsEmptyPicture()` and `operator==` both **switch on `m_type`**, because "empty" and "equal"
mean different things per kind:

| Kind | Empty when | Equal when |
|---|---|---|
| `eFromBackend` | `m_class_identifier == 0` | same id |
| `eFromConfiguration` | `!m_meta_guid.isValid()` | same guid |
| `eFromFile` | `width == 0 && height == 0` | same **bytes** + size |

Note the cost asymmetry: comparing two file pictures compares their buffers; comparing two
engine pictures compares an integer. That `operator==` is what makes a picture property
diffable through the variant ([property-system.md § 6.1](property-system.md)).

The file payload is deliberately small and self-contained:

```cpp
struct ibExternalPictureDescription {
    std::string m_img_name, m_img_buffer;      // name + the encoded image bytes
    unsigned int m_width = 0, m_height = 0;
};
```

and it serialises through the node tree, not as an opaque blob:

```cpp
class BACKEND_API ibExternalPictureDescriptionMemory {
    // node form: a Child { Name, Buffer(image bytes), Width, Height }
    static bool ReadNode(const ibDataValue& value, ibExternalPictureDescription& pictureDesc);
    static bool WriteNode(ibDataValue& value, const ibExternalPictureDescription& pictureDesc);
};
```

---

## 3. `ibBackendPicture` — the registry and the resolver

`backend_picture.h`. A **static-only** class (`ibBackendPicture() = delete;`) with three
jobs.

### 3.1 Registry — engine pictures

```cpp
struct ibBackendPictureEntry { wxString m_name; wxBitmap m_data; ibPictureID m_id; };

static void RegisterPicture(const wxString name, const ibPictureID& id, const wxBitmap& bitmap);
static bool IsRegisterPicture(const ibPictureID& id);
static wxBitmap GetPicture(const ibPictureID& id);
static wxIcon    GetPictureAsIcon(const ibPictureID& id);
static std::vector<ibBackendPictureEntry> GetArrayPicture();
```

`GetArrayPicture()` is what lets a picture chooser list everything the engine has.

### 3.2 Resolver — description → bitmap

```cpp
static wxBitmap CreatePicture(const ibExternalPictureDescription& pictureDesc, const wxSize& size = wxSize(16, 16));
static wxBitmap CreatePicture(const ibPictureDescription& pictureDesc,
                              const ibMetaData* metaData = nullptr, const wxSize& size = wxSize(16, 16));
```

**One call resolves all three kinds** — and the `metaData` parameter is why: an
`eFromConfiguration` picture is a guid that only means something against an open
configuration. Pass `nullptr` and a configuration picture cannot resolve; engine and file
pictures do not care. The default `16x16` says what these are mostly for: icons.

### 3.3 Base64 conversion

```cpp
static wxString CreateBase64Image(const wxImage& image);
static wxImage  GetImageFromBase64(const wxString& src, const wxSize& size = wxDefaultSize);
static wxBitmap GetBitmapFromBase64(const wxString& src, const wxSize& size = wxDefaultSize);
static wxIcon   GetIconFromBase64(const wxString& src, const wxSize& size = wxDefaultSize);
```

Base64 is the transport form — how an image survives a text-ish channel (the web front, a
JSON view, an embedded literal).

---

## 4. Predefined engine pictures

`picturePredefined.h` — **26** compile-time ids:

```cpp
constexpr ibPictureID g_picStructureCLSID  = picture_to_clsid("PC_STRCT");
constexpr ibPictureID g_picErrorCLSID      = picture_to_clsid("PC_ERROR");
constexpr ibPictureID g_picCloseFormCLSID  = picture_to_clsid("PC_CLOSE");
constexpr ibPictureID g_picUpdateFormCLSID = picture_to_clsid("PC_REFRE");
constexpr ibPictureID g_picHelpFormCLSID   = picture_to_clsid("PC_HELP");
constexpr ibPictureID g_picAddCLSID        = picture_to_clsid("PC_ADDVL");
constexpr ibPictureID g_picEditCLSID       = picture_to_clsid("PC_EDITV");
constexpr ibPictureID g_picCopyCLSID       = picture_to_clsid("PC_COPYV");
constexpr ibPictureID g_picDeleteCLSID     = picture_to_clsid("PC_DELVL");
constexpr ibPictureID g_picAddFolderCLSID  = picture_to_clsid("PC_ADDFV");
…
```

**Pictures are a CLSID kind of their own** — `picture_to_clsid` is a member of the
registrar family ([../CLAUDE.md](../CLAUDE.md) §6), so an id carries "I am a picture" in its
high byte. `constexpr` means these cost nothing at runtime and can be used as default
arguments.

The `"PC_XXXXX"` string is an **opaque key**, like every other clsid key — not a label.

### 4.1 Recipe — adding a new engine picture (PNG → base64)

Engine pictures are **embedded as base64-encoded PNG string literals** — no external files,
no resource compilation. The bytes travel inside `backend.dll`. Adding one is four edits, all
in the picture-predefined pair (`picturePredefined.h` / `.cpp`):

**Step 1 — encode the PNG to base64.** A 16×16 PNG is the norm (these are icons). Any base64
tool works; the string is the raw PNG bytes, base64'd, no `data:` prefix, no line breaks:

```bash
base64 -w0 myicon_16.png            # POSIX
# or [Convert]::ToBase64String([IO.File]::ReadAllBytes('myicon_16.png'))   # PowerShell
```

**Step 2 — add the literal** inside the `__predefined_png__` region of `picturePredefined.cpp`:

```cpp
/* PNG */
static const wxString s_myIcon_16_png = "iVBORw0KGgoAAAANS...==";   // your base64
```

**Step 3 — declare the CLSID** in `picturePredefined.h` (5-char opaque key, `PC_` family):

```cpp
constexpr ibPictureID g_picMyIconCLSID = picture_to_clsid("PC_MYICO");
```

`picture_to_clsid` stamps the picture KIND into the id's high byte
([../CLAUDE.md](../CLAUDE.md) §6); `constexpr` means it costs nothing and works as a default
argument. The `"PC_MYICO"` key only has to be unique **within the picture kind** (the registry
duplicate-check catches a collision).

**Step 4 — register it** in `RegisterAllBackendPicture()` (bottom of `picturePredefined.cpp`):

```cpp
::RegisterBackendPicture(wxT("MyIcon"), g_picMyIconCLSID, s_myIcon_16_png);
```

`RegisterBackendPicture(name, id, base64)` (`backend_picture.cpp`) decodes the base64 via
`ibBackendPicture::GetImageFromBase64` and calls `ibBackendPicture::RegisterPicture` — but
only `if (!IsRegisterPicture(id))`, so double-registration is a no-op. The `name` is what a
picture chooser shows (`GetArrayPicture()`).

**No wiring needed** — `RegisterAllBackendPicture()` runs from the ctor of a static
`ibBackendPictureAutoLoader` (which also calls `wxInitAllImageHandlers()` so PNG decoding
works), so every engine picture is live before `main()`. Use it anywhere a description takes
an engine id:

```cpp
ibPictureDescription desc(g_picMyIconCLSID);                 // tagged eFromBackend (§2)
wxBitmap bmp = ibBackendPicture::CreatePicture(g_picMyIconCLSID);   // resolve to bytes (§3.2)
```

> There are three `RegisterBackendPicture` overloads (`backend_picture.cpp`): raw bytes
> (`wxImage(data)`), **base64** (the one used here), and a ready `wxBitmap`. New engine icons
> take the base64 path.

---

## 5. The other surfaces

| Surface | Class | Notes |
|---|---|---|
| **Metaobject** | `ibValueMetaObjectPicture : ibValueMetaObject` | a Picture in the configuration → `eFromConfiguration` |
| **Property** | `ibPropertyPicture` (+ `propertyPictureExt.cpp`) | editable in the inspector; `GetValueAsBitmap()` |
| **Script value** | `StoragePicture` (`ibValuePicture`) | [script-value-types.md § 2.9](script-value-types.md) |
| **Commands** | `ibPictureDescription m_pictureDescription` on `ibCommandItem` | every command carries one ([command-interface.md § 5](command-interface.md)) |

The fallback idiom appears wherever a picture is optional
([command-interface.md § 2](command-interface.md)):

```cpp
wxBitmap GetPictureAsBitmap() const {
    if (!m_propertyPicture->IsEmptyProperty())
        return m_propertyPicture->GetValueAsBitmap();
    return ibBackendPicture::CreatePicture(g_metaCommonMetadataCLSID);   // engine default
}
```

— a user picture if set, an engine picture otherwise. Never nothing.

---

## 6. Honest remainder

- ⚠ **`wxBitmap` / `wxIcon` / `wxImage` appear in `backend.dll`**, which
  [../CLAUDE.md](../CLAUDE.md) declares GUI-free ("do not add `#include` for wxWidgets
  headers in `backend.dll` — the backend must remain GUI-free"). These are wxCore types, not
  wxBase. The picture subsystem is a **standing exception** to that rule; whether it is
  intended or historical is worth deciding explicitly, because it is the one place the
  backend/frontend split leaks. Compare with how the spreadsheet
  ([report-engine.md § 3](report-engine.md)) and property system
  ([property-system.md § 4](property-system.md)) went to lengths — a notifier and a function
  slot — to avoid exactly this.
- `ibExternalPictureDescription::operator==` contains an unreachable second `return false;`
  after its `return`-expression — harmless, and a marker that it was edited.
- `m_img_buffer` is a `std::string` holding binary — fine, but it means "image bytes" and
  "text" share a type at this seam.
