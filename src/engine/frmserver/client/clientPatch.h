#ifndef __CLIENT_PATCH_H__
#define __CLIENT_PATCH_H__

// THE PATCH — what turns the frame a client holds into the one drawn now, said by node: a client that names the
// frame it holds (Since) is answered with it instead of the whole (ibClientHost). Built over the frame's own shape,
// so it grows with the frame and needs no list of what may change.
//
// A patch is a node of the frame's shape carrying only what changed, and a client applies it to its copy:
//   - `NodeRemoved` names the entries gone, taken out first; then an entry (a field, a named sub-node) the patch
//     carries is set — merged into the copy's own when both are sub-nodes, set whole otherwise;
//   - children named by their ids (NodeId: a form's controls, the tabs): a patch child the copy has is merged into
//     it, one it has not is new and inserted whole; `NodeRemovedIds` names the children gone, taken out first, and
//     `NodeOrder` gives the order of all of them whenever it changed or something was inserted;
//   - children without ids (a menu, the schemas' lists) come whole when anything among them changed:
//     `NodeChildrenWhole`, and the patch's children are the list.
// A node's identity (NodeType, NodeId) is the patch node's.

#include "core/serialize/dataBuilder.h"   // ibDataNode — what a frame and its patch are

#include "frmserver/frmserver.h"

// The patch from `from` to `to` into `patch` (empty when they are equal). False when the change cannot be said as
// one — a node's raw block differs — or says no less than `to` itself: the frame then goes whole, so a patch is never
// heavier than the frame.
FRMSERVER_API bool ibClientFramePatch(const ibDataNode& from, const ibDataNode& to, ibDataNode& patch);

#endif
