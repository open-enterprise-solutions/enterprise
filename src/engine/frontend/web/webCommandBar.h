#ifndef __WEB_COMMAND_BAR_H__
#define __WEB_COMMAND_BAR_H__

// One place that turns a command STORE into the toolbar node a browser draws.
//
// There are two of them on a form and they were never the same code: the form's
// own strip, built in the web visual host, and a control's -- the one the
// desktop puts above a tablebox, which the web build did not build at all,
// because ibValueWindowComposite::CreateWithLayers assembles its layers only
// under #ifndef OES_USE_WEB. A tabular section therefore arrived as a grid with
// no way to add a line to it.
//
// The nodes hold no back-pointer. A tool names its command by action id and the
// bar it belongs to by the OWNER's control id -- zero for the form's own, which
// is what ibWebApplication::DispatchCommand resolves against when nothing else
// is named. Ids are unique per bar, not across bars, so the owner is not
// decoration: without it a table's "Add" and a form's would be the same number.

class ibValueCommandBar;
class ibMetaData;
class ibWebToolbar;

// The toolbar node for this bar, or nullptr when there is nothing to draw --
// no bar, or a bar whose commands all resolved away. The caller owns what
// comes back.
ibWebToolbar* ibWebBuildCommandBar(ibValueCommandBar* commandBar,
	const ibMetaData* metaData, int ownerControlId);

#endif
