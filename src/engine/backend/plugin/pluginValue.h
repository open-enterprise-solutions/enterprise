/////////////////////////////////////////////////////////////////////////////
// Host-side definition of the opaque ibPluginValue type declared in the
// ABI header (pluginApi.h). Plugins manipulate values only through the
// ibHostAPI Make* / Get* function pointers; they never see the struct
// layout. The host stores an ibValue plus a side buffer that backs the
// UTF-8 lifetime contract of GetString.
//
// Lifetime is arena-scoped: every ibPluginValue allocated inside a
// single RegisterFunction / event callback is freed when that callback
// returns. The arena lives in ibPluginCallScope; plugins must not
// stash returned pointers across callbacks.
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_PLUGIN_VALUE_H_
#define _IB_PLUGIN_VALUE_H_

#include "backend/backend.h"
#include "backend/compiler/value.h"
#include "backend/plugin/pluginApi.h"

#include <memory>
#include <string>
#include <vector>

// Host-side body of the opaque struct. The forward declaration in
// pluginApi.h (`typedef struct ibPluginValue_s ibPluginValue`) wires
// pointer compatibility; this definition is internal.
struct ibPluginValue_s {
	ibValue     m_value;
	// Backing buffer for GetString — kept alive until the arena frees
	// the value. We can't return m_value.GetString().mb_str() directly
	// because that produces a temporary buffer.
	std::string m_utf8Cache;
};

class BACKEND_API ibPluginCallScope {
public:
	ibPluginCallScope() = default;
	~ibPluginCallScope() = default;
	ibPluginCallScope(const ibPluginCallScope&) = delete;
	ibPluginCallScope& operator=(const ibPluginCallScope&) = delete;

	ibPluginValue* MakeString(const char* utf8);
	ibPluginValue* MakeNumber(double n);
	ibPluginValue* MakeBool(int b);
	ibPluginValue* MakeNull();

	// Adopt an externally-built ibValue (host pushing args into a
	// callback). Pointer ownership transfers to the arena.
	ibPluginValue* AdoptFromValue(const ibValue& v);

	// Read accessors used by the host bridge to forward to plugins.
	static const char* GetString(const ibPluginValue* v);
	static double      GetNumber(const ibPluginValue* v);
	static int         GetBool  (const ibPluginValue* v);
	static int         IsNull   (const ibPluginValue* v);

private:
	std::vector<std::unique_ptr<ibPluginValue_s>> m_pool;
};

#endif // _IB_PLUGIN_VALUE_H_
