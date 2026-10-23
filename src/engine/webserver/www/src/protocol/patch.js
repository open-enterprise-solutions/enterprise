// A PATCH applied to the copy of the frame the page holds — what turns that frame into the one drawn now
// (docs/public/client-protocol.md, The frame). The same rules as the thin client's, src/engine/protocol/framePatch.cpp,
// line for line: the entries gone first, then each carried — merged when both are nodes; children with ids merged by
// their id, those gone taken out first, in the order given; children without ids come whole.

/** @typedef {{[name: string]: any, NodeId?: number, NodeType?: string, NodeChildren?: ProtocolNode[]}} ProtocolNode */

// The words a node is written with on the wire, and the patch's own — what the patch says of the node and its
// children, never an entry of it.
const structural = new Set(['NodeRemoved', 'NodeRemovedIds', 'NodeOrder', 'NodeChildrenWhole', 'NodeChildren',
	'NodeType', 'NodeId']);

/** @param {any} value */
function isNode(value) {
	return value !== null && typeof value === 'object' && !Array.isArray(value);
}

/** A child's id — 0 for one that has none. @param {any} node */
function idOf(node) {
	return isNode(node) && Number.isInteger(node.NodeId) ? node.NodeId : 0;
}

/**
 * @param {ProtocolNode} copy the node held, changed in place
 * @param {ProtocolNode} patch
 */
export function applyPatch(copy, patch) {
	for (const name of patch.NodeRemoved ?? [])
		delete copy[name];
	for (const [name, value] of Object.entries(patch)) {
		if (structural.has(name))
			continue;
		if (isNode(value) && isNode(copy[name]))
			applyPatch(copy[name], value);
		else
			copy[name] = structuredClone(value);
	}
	for (const identity of ['NodeType', 'NodeId']) {
		if (patch[identity] !== undefined)
			copy[identity] = patch[identity];
	}

	// Children without ids — whole, as the patch carries them.
	if (patch.NodeChildrenWhole === true) {
		copy.NodeChildren = structuredClone(patch.NodeChildren ?? []);
		return;
	}
	const removedIds = patch.NodeRemovedIds;
	const order = patch.NodeOrder;
	const patched = patch.NodeChildren;
	if (patched === undefined && removedIds === undefined && order === undefined)
		return;

	let kids = copy.NodeChildren ?? [];
	// Children by their ids: those gone taken out first; then each the patch carries merged into the one held, or in
	// whole when none is (a child gone and back under its id was taken out above)…
	if (removedIds !== undefined) {
		const gone = new Set(removedIds);
		kids = kids.filter((kid) => !gone.has(idOf(kid)));
	}
	for (const child of patched ?? []) {
		const id = idOf(child);
		const held = kids.find((kid) => idOf(kid) === id);
		if (held === undefined)
			kids.push(structuredClone(child));
		else if (isNode(child))
			applyPatch(held, child);
	}
	// …in the order the patch gives, when it gives one: whenever the order changed or something came in.
	if (order !== undefined) {
		const position = new Map(order.map((id, at) => [id, at]));
		const at = (kid) => position.get(idOf(kid)) ?? position.size;
		kids = kids.map((kid, index) => ({ kid, index }))
			.sort((left, right) => at(left.kid) - at(right.kid) || left.index - right.index)
			.map(({ kid }) => kid);
	}
	copy.NodeChildren = kids;
}
