// THE SECTION PANEL (schema 3) — the thin client's (enterprise-thin/mainFrame/mainFrameEnterpriseInterface.cpp) in
// the browser: a column of section buttons down the window's left side, and a section's page opened beside them, over
// the rest of the window. The page lays the section's blocks in two columns, as the server orders them
// (frmserver/schema/runtime/sections.cpp, PageOf): on the left the section's own items under no heading, the navigation
// groups and the subsections; on the right the actions — Create, Reports, Service, then the declared groups. A link
// opens its item through the schema's command, Open {Item, Type}, and the page closes once it has. A press outside the
// page, Esc, or the open section's button again puts it away; another section's button opens that one instead. A
// narrow window lays the buttons in a strip under its title, and the page under the strip, its columns one under the
// other.

/** @typedef {import('../protocol/session.js').Session} Session */
/** @typedef {import('../protocol/pictures.js').Pictures} Pictures */
/** @typedef {import('../protocol/patch.js').ProtocolNode} ProtocolNode */

const schema = 3;
const open = 1;
// A block's column (ibSections::ibColumn): the actions'.
const right = 2;

export class Sections {
	/**
	 * @param {Session} session
	 * @param {Pictures} pictures
	 * @param {(error: Error) => void} refused  a refusal, told to the person
	 */
	constructor(session, pictures, refused) {
		this.session = session;
		this.pictures = pictures;
		this.refused = refused;
		this.element = document.createElement('nav');
		this.element.className = 'sections';
		this.element.hidden = true;
	}

	/**
	 * The panel read and drawn when the frame's application offers it — a server that does not is not asked, and the
	 * window goes without the panel (ibFrontendMainFrameEnterprise::CreateSubSystem). Only the sections the person
	 * may use come.
	 * @param {ProtocolNode} frame
	 */
	async read(frame) {
		if (!(frame.Schemas ?? []).includes(schema))
			return;
		const shown = await this.session.call('schema', { Schema: schema });
		await this.pictures.fetchFor(shown).catch(() => {});
		const sections = shown.Sections?.NodeChildren ?? [];
		for (const section of sections)
			this.element.append(...this.button(section));
		this.element.hidden = sections.length === 0;
	}

	/**
	 * A section's button and the page it opens: its picture and title, cut short with an ellipsis when the column is
	 * narrower — the whole title on hover; the open one white and bold.
	 * @param {ProtocolNode} section
	 */
	button(section) {
		const page = this.page(section);
		const button = document.createElement('button');
		button.type = 'button';
		button.className = 'section';
		button.title = section.Title ?? '';
		button.setAttribute('aria-expanded', 'false');
		button.popoverTargetElement = page;
		const face = document.createElement('span');
		face.className = 'face';
		const picture = this.pictures.url(section.Picture);
		if (picture) {
			const image = document.createElement('img');
			image.src = picture;
			image.alt = '';
			face.append(image);
		}
		const title = document.createElement('span');
		title.className = 'title';
		title.textContent = section.Title ?? '';
		face.append(title);
		button.append(face);
		page.addEventListener('beforetoggle', (/** @type {any} */ event) => {
			if (event.newState === 'open')
				this.place(page);
		});
		page.addEventListener('toggle', (/** @type {any} */ event) => {
			button.setAttribute('aria-expanded', String(event.newState === 'open'));
		});
		return [button, page];
	}

	/**
	 * A section's page: its blocks, each in its column.
	 * @param {ProtocolNode} section
	 */
	page(section) {
		const page = document.createElement('div');
		page.className = 'section-page';
		page.popover = 'auto';
		const navigation = document.createElement('div');
		const actions = document.createElement('div');
		navigation.className = actions.className = 'column';
		for (const block of section.Blocks?.NodeChildren ?? [])
			(block.Column === right ? actions : navigation).append(this.block(block, page));
		page.append(navigation, actions);
		return page;
	}

	/**
	 * One block: its heading — a declared group's picture beside it and its tooltip on both — and its links under it;
	 * the section's own items stand at the top under no heading.
	 * @param {ProtocolNode} block
	 * @param {HTMLElement} page
	 */
	block(block, page) {
		const element = document.createElement('div');
		element.className = 'block';
		if (block.Title) {
			const heading = document.createElement('h3');
			const picture = this.pictures.url(block.Picture);
			if (picture) {
				const image = document.createElement('img');
				image.src = picture;
				image.alt = '';
				heading.append(image);
			}
			heading.append(block.Title);
			if (block.Tooltip)
				heading.title = block.Tooltip;
			element.append(heading);
		}
		const links = document.createElement('div');
		links.className = 'links';
		for (const item of block.NodeChildren ?? [])
			links.append(this.link(item, page));
		element.append(links);
		return element;
	}

	/**
	 * A command of the page: its icon and caption, no button around them, underlined while the pointer is over it.
	 * @param {ProtocolNode} item
	 * @param {HTMLElement} page
	 */
	link(item, page) {
		const link = document.createElement('button');
		link.type = 'button';
		link.className = 'link';
		const icon = this.pictures.url(item.Icon);
		if (icon) {
			const image = document.createElement('img');
			image.src = icon;
			image.alt = '';
			link.append(image);
		}
		const caption = document.createElement('span');
		caption.textContent = item.Title ?? '';
		link.append(caption);
		link.addEventListener('click', async () => {
			try {
				await this.session.call('schema', { Schema: schema, Command: open,
					Args: { Item: item.Item, Type: item.Type } });
				page.hidePopover();
			}
			catch (error) {
				this.refused(/** @type {Error} */ (error));
			}
		});
		return link;
	}

	/**
	 * The page beside the buttons, over the rest of the window down to their end; under the strip on a narrow window.
	 * @param {HTMLElement} page
	 */
	place(page) {
		const panel = this.element.getBoundingClientRect();
		const width = document.documentElement.clientWidth;
		const height = document.documentElement.clientHeight;
		const strip = getComputedStyle(this.element).flexDirection === 'row';
		const left = strip ? 0 : panel.right;
		const top = strip ? panel.bottom : panel.top;
		page.style.left = `${left}px`;
		page.style.top = `${top}px`;
		page.style.width = `${width - left}px`;
		page.style.height = `${(strip ? height : panel.bottom) - top}px`;
	}
}
