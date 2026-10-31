import type { PropertyValueMap, PropertyValues } from 'lit';
import WebAwesomeElement from '../../internal/webawesome-element.js';
import '../checkbox/checkbox.js';
import '../icon/icon.js';
import '../spinner/spinner.js';
export type TreeItemContext = {
    depth: number;
    expanded: boolean;
};
export declare const treeItemContext: {
    __context__: TreeItemContext;
};
/**
 * @summary Tree items represent a single hierarchical node inside a tree, and can contain nested items that expand and
 *  collapse.
 * @documentation https://webawesome.com/docs/components/tree-item
 * @status stable
 * @since 2.0
 *
 * @dependency wa-checkbox
 * @dependency wa-icon
 * @dependency wa-spinner
 *
 * @event wa-expand - Emitted when the tree item expands.
 * @event wa-after-expand - Emitted after the tree item expands and all animations are complete.
 * @event wa-collapse - Emitted when the tree item collapses.
 * @event wa-after-collapse - Emitted after the tree item collapses and all animations are complete.
 * @event wa-lazy-change - Emitted when the tree item's lazy state changes.
 * @event wa-lazy-load - Emitted when a lazy item is selected. Use this event to asynchronously load data and append
 *  items to the tree before expanding. After appending new items, remove the `lazy` attribute to remove the loading
 *  state and update the tree.
 *
 * @slot - The default slot.
 * @slot expand-icon - The icon to show when the tree item is expanded.
 * @slot collapse-icon - The icon to show when the tree item is collapsed.
 *
 * @csspart base - Deprecated. Use the `tree-item` part instead.
 * @csspart tree-item - The component's outer wrapper.
 * @csspart item - The tree item's container. This element wraps everything except slotted tree item children.
 * @csspart indentation - The tree item's indentation container.
 * @csspart expand-button - The container that wraps the tree item's expand button and spinner.
 * @csspart spinner - The spinner that shows when a lazy tree item is in the loading state.
 * @csspart spinner__base - The spinner's base part.
 * @csspart label - The tree item's label.
 * @csspart children - The container that wraps the tree item's nested children.
 * @csspart checkbox - The checkbox that shows when using multiselect.
 * @csspart checkbox__base - The checkbox's exported `base` part.
 * @csspart checkbox__control - The checkbox's exported `control` part.
 * @csspart checkbox__checked-icon - The checkbox's exported `checked-icon` part.
 * @csspart checkbox__indeterminate-icon - The checkbox's exported `indeterminate-icon` part.
 * @csspart checkbox__label - The checkbox's exported `label` part.
 *
 * @cssproperty [--show-duration=var(--wa-transition-normal)] - The animation duration when expanding tree items.
 * @cssproperty [--hide-duration=var(--wa-transition-normal)] - The animation duration when collapsing tree items.
 *
 * @cssstate disabled - Applied when the tree item is disabled.
 * @cssstate expanded - Applied when the tree item is expanded.
 * @cssstate indeterminate - Applied when the selection is indeterminate.
 * @cssstate selected - Applied when the tree item is selected.
 */
export default class WaTreeItem extends WebAwesomeElement {
    static css: import("lit").CSSResult;
    static isTreeItem(node: Node): boolean;
    private readonly localize;
    indeterminate: boolean;
    isLeaf: boolean;
    loading: boolean;
    selectable: boolean;
    /** Expands the tree item. */
    expanded: boolean;
    /** Draws the tree item in a selected state. */
    selected: boolean;
    /** Disables the tree item. */
    disabled: boolean;
    /** Enables lazy loading behavior. */
    lazy: boolean;
    _treeItemContext: TreeItemContext;
    _parentTreeContext: TreeItemContext | null;
    private animationGeneration;
    defaultSlot: HTMLSlotElement;
    childrenSlot: HTMLSlotElement;
    itemElement: HTMLDivElement;
    childrenContainer: HTMLDivElement;
    expandButtonSlot: HTMLSlotElement;
    tabIndex: number;
    role: string;
    connectedCallback(): void;
    firstUpdated(changedProperties: PropertyValues<typeof this>): void;
    private animateCollapse;
    private isNestedItem;
    /** Counts the nesting depth and sets the private --indent property on the host for indentation. */
    private updateIndentation;
    private getDepth;
    private handleChildrenSlotChange;
    protected willUpdate(changedProperties: PropertyValueMap<WaTreeItem> | Map<PropertyKey, unknown>): void;
    private animateExpand;
    handleLoadingChange(): void;
    handleDisabledChange(): void;
    handleExpandedState(): void;
    handleIndeterminateStateChange(): void;
    handleSelectedChange(): void;
    handleExpandedChange(): void;
    handleExpandAnimation(): void;
    handleLazyChange(): void;
    /** Gets all the nested tree items in this node. */
    getChildrenItems({ includeDisabled }?: {
        includeDisabled?: boolean;
    }): WaTreeItem[];
    render(): import("lit-html").TemplateResult<1>;
}
declare global {
    interface HTMLElementTagNameMap {
        'wa-tree-item': WaTreeItem;
    }
}
