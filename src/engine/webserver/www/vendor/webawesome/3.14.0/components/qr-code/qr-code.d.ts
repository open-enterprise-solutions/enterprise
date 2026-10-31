import type { PropertyValues } from 'lit';
import WebAwesomeElement from '../../internal/webawesome-element.js';
/**
 * @summary QR codes encode a URL or other short text into a scannable image, rendered client-side using the Canvas API.
 *  Use them to share links, contact info, or Wi-Fi credentials that visitors can scan with a phone.
 * @documentation https://webawesome.com/docs/components/qr-code
 * @status stable
 * @since 2.0
 *
 * @csspart base - Deprecated. Use the `qr-code` part instead.
 * @csspart qr-code - The component's outer wrapper.
 */
export default class WaQrCode extends WebAwesomeElement {
    static css: import("lit").CSSResult;
    canvas: HTMLCanvasElement;
    /** The QR code's value. */
    value: string;
    /** The label for assistive devices to announce. If unspecified, the value will be used instead. */
    label: string;
    /** The size of the QR code, in pixels. */
    size: number;
    /**
     * The fill color. This can be any valid CSS color, but not a CSS custom property.
     * @deprecated Use the `color` CSS property instead.
     */
    fill: string;
    /**
     * The background color. This can be any valid CSS color or `transparent`. It cannot be a CSS custom property.
     * @deprecated Use the `background` or `background-color` CSS property on the host element instead.
     */
    background: string;
    /** The edge radius of each module. Must be between 0 and 0.5. */
    radius: number;
    /** The level of error correction to use. [Learn more](https://www.qrcode.com/en/about/error_correction.html) */
    errorCorrection: 'L' | 'M' | 'Q' | 'H';
    image: string | null;
    imageBackground: string | null;
    imageCoverage: number | null;
    imagePadding: number | null;
    private computedStyle;
    updated(changedProperties: PropertyValues<this>): void;
    generate(): void;
    render(): import("lit-html").TemplateResult<1>;
}
declare global {
    interface HTMLElementTagNameMap {
        'wa-qr-code': WaQrCode;
    }
}
