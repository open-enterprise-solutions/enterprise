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
