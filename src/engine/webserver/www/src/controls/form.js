// THE FORM — the root of a tab's view (ClientForm): its command bar on top, its controls under it laid as its
// orientation says. Its caption is the tab's, drawn by the window; its Key is what every event of its controls names
// it by. It is the container the narrow screen is measured on (styles/form.css): its width, not the window's, decides
// when a row folds into a column.
//
// The controls are drawn in a window of their own under the bar, as the thin client's are (ibVisualHostClient,
// m_contentWindow): they fill it while they fit and scroll in it when they do not, and the bar stays where it is.

import { Control, defineControl, reconcileChildren } from './control.js';
import { CommandBar } from './commandBar.js';
import { alignLabels } from './labels.js';

/** @typedef {import('../protocol/patch.js').ProtocolNode} ProtocolNode */

// A box's orientation (wxHORIZONTAL).
const horizontal = 4;

// A form's VerticalScroll (ibVerticalScroll: UseIfNecessary, Use, DontUse), as the content's overflow.
const verticalScroll = ['auto', 'scroll', 'hidden'];

class ClientForm extends Control {
	/** @param {ProtocolNode} node */
	build(node) {
		this.bar = new CommandBar(this);
		this.content = document.createElement('div');
		this.content.className = 'content';
		this.append(this.bar.element, this.content);
		this.update(node);
	}

	/** @param {ProtocolNode} node */
	update(node) {
		this.setAttribute('orient', node.Orient === horizontal ? 'horizontal' : 'vertical');
		// The narrowest and lowest its controls are laid out (0 — as they allow): a window smaller scrolls them.
		this.content.style.setProperty('--oes-form-width', `${node.Width > 0 ? node.Width : 0}px`);
		this.content.style.setProperty('--oes-form-height', `${node.Height > 0 ? node.Height : 0}px`);
		// Up and down: a bar when needed, always one, or none — then what does not fit is cut off.
		this.setAttribute('vertical-scroll', verticalScroll[node.VerticalScroll ?? 0] ?? 'auto');
		this.bar.update(this.state(node).CommandBar);
		return true;
	}

	/**
	 * Its controls, and the label columns over them once they are all drawn — the thin client lines them up after
	 * every update of the form too (ibVisualHostClient::AlignLabels).
	 * @param {ProtocolNode} node
	 */
	drawChildren(node) {
		reconcileChildren(this.content, node.NodeChildren ?? [], this.context);
		alignLabels(this);
	}
}

defineControl('ClientForm', ClientForm);
