// THE SIZERS — how a form lays its controls out, as the desktop's sizers do: a box (a row or a column), a wrapping
// row, a grid, a box with a frame and a caption; and an item, which says how the control it holds takes its place —
// its share of the free space (Proportion), whether it stretches across (Stretch), the border around it. A box laid
// as a row folds into a column on a narrow screen (styles/form.css): a form drawn for a desktop still reads on a phone.

import { Control, defineControl } from './control.js';

/** @typedef {import('../protocol/patch.js').ProtocolNode} ProtocolNode */

// A box's orientation (wxHORIZONTAL, wxVERTICAL).
const horizontal = 4;
// An item's Stretch (wxStretch): Expand fills the box across, Shrink keeps the control's own size.
const expand = 0x2000;

class SizerItem extends Control {
	/** @param {ProtocolNode} node */
	update(node) {
		const proportion = node.Proportion ?? 0;
		this.style.flexGrow = proportion > 0 ? String(proportion) : '';
		this.style.alignSelf = ((node.Stretch ?? 0) & expand) !== 0 ? 'stretch' : 'flex-start';
		const border = node.BorderSize ?? 0;
		this.style.marginLeft = node.BorderLeft && border > 0 ? `${border}px` : '';
		this.style.marginRight = node.BorderRight && border > 0 ? `${border}px` : '';
		this.style.marginTop = node.BorderTop && border > 0 ? `${border}px` : '';
		this.style.marginBottom = node.BorderBottom && border > 0 ? `${border}px` : '';
		this.classList.toggle('grows', proportion > 0);
		return true;
	}

	/** @param {ProtocolNode} node */
	build(node) {
		this.update(node);
	}
}

class Boxsizer extends Control {
	/** @param {ProtocolNode} node */
	update(node) {
		this.setAttribute('orient', node.Orient === horizontal ? 'horizontal' : 'vertical');
		return true;
	}

	/** @param {ProtocolNode} node */
	build(node) {
		this.update(node);
	}
}

class Wrapsizer extends Boxsizer {}

class Gridsizer extends Control {
	/** @param {ProtocolNode} node */
	update(node) {
		this.style.gridTemplateColumns = `repeat(${Math.max(1, node.Cols ?? 1)}, auto)`;
		return true;
	}

	/** @param {ProtocolNode} node */
	build(node) {
		this.update(node);
	}
}

// The legends drawn so far — each one's id, which its box is named by to a reader of the screen.
let legends = 0;

class Staticboxsizer extends Boxsizer {
	/** @param {ProtocolNode} node */
	build(node) {
		this.legend = document.createElement('div');
		this.legend.className = 'legend';
		this.legend.id = `oes-legend-${++legends}`;
		this.setAttribute('role', 'group');
		this.setAttribute('aria-labelledby', this.legend.id);
		this.append(this.legend);
		this.update(node);
	}

	/** @param {ProtocolNode} node */
	update(node) {
		super.update(node);
		this.legend.textContent = this.state(node).Title ?? node.Title ?? '';
		return true;
	}
}

defineControl('SizerItem', SizerItem);
defineControl('Boxsizer', Boxsizer);
defineControl('Wrapsizer', Wrapsizer);
defineControl('Gridsizer', Gridsizer);
defineControl('Staticboxsizer', Staticboxsizer);
