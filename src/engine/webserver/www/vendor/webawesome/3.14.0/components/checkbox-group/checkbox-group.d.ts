import type { PropertyValues } from 'lit';
import WebAwesomeElement from '../../internal/webawesome-element.js';
import '../checkbox/checkbox.js';
/**
 * @summary Checkbox groups wrap a set of related checkboxes or switches so they share a label, hint, and grouping
 *  semantics.
 * @documentation https://webawesome.com/docs/components/checkbox-group
 * @status stable
 * @since 3.9
 *
 * @dependency wa-checkbox
 *
 * @slot - The default slot where `<wa-checkbox>` or `<wa-switch>` elements are placed.
 * @slot label - The checkbox group's label. Required for proper accessibility. Alternatively, you can use the `label`
 *  attribute.
 * @slot hint - Text that describes how to use the checkbox group. Alternatively, you can use the `hint` attribute.
 *
 * @csspart form-control - The form control that wraps the label, group, and hint.
 * @csspart form-control-label - The label.
 * @csspart form-control-input - The element that wraps the grouped checkboxes, exposed as a `role="group"`.
 * @csspart hint - The hint's wrapper.
 *
 * @cssproperty [--gap=0.5em] - The gap between grouped checkboxes.
 */
export default class WaCheckboxGroup extends WebAwesomeElement {
    static css: import("lit").CSSResult[];
    private readonly hasSlotController;
    /**
     * The checkbox group's label. Required for proper accessibility. If you need to display HTML, use the `label` slot
     * instead.
     */
    label: string;
    /** The checkbox group's hint. If you need to display HTML, use the `hint` slot instead. */
    hint: string;
    /** The orientation in which to show grouped checkboxes. */
    orientation: 'horizontal' | 'vertical';
    /**
     * The group's size. When present, this size will be applied to all `<wa-checkbox>` and `<wa-switch>` items inside.
     */
    size: 'xs' | 's' | 'm' | 'l' | 'xl' | 'small' | 'medium' | 'large';
    handleSizeChange(): void;
    /**
     * Indicates that at least one option should be selected. This only adds a visual indicator to the label. To enforce
     * the requirement, use the `required` attribute on the individual checkboxes and/or their `setCustomValidity()`
     * method.
     */
    required: boolean;
    /**
     * Only required for SSR. Set to `true` if you're slotting in a `label` element so the server-rendered markup includes
     * the label before the component hydrates on the client.
     */
    withLabel: boolean;
    /**
     * Only required for SSR. Set to `true` if you're slotting in a `hint` element so the server-rendered markup includes
     * the hint before the component hydrates on the client.
     */
    withHint: boolean;
    updated(changedProperties: PropertyValues<this>): void;
    /** Returns all grouped checkbox and switch elements. */
    private getAllCheckboxes;
    /**
     * Applies the group's size to each grouped checkbox/switch
     */
    private syncCheckboxElements;
    render(): import("lit-html").TemplateResult<1>;
}
declare global {
    interface HTMLElementTagNameMap {
        'wa-checkbox-group': WaCheckboxGroup;
    }
}
