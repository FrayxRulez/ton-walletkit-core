//
// Copyright (c) Fela Ameghino 2026
//
// Distributed under the MIT License. (See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT)
//

/**
 * EventSource over the host's SSE delegate — the transport TON Connect uses to
 * receive dapp requests through the relay.
 *
 * The host owns the text/event-stream framing and hands us one dispatched event
 * per callback as {"data","event","id"}; this class turns those into the DOM
 * EventSource surface walletkit expects (onmessage/onerror/addEventListener).
 */

/* eslint-disable @typescript-eslint/no-explicit-any */

declare const __twk_sse_open: (
    url: string,
    headersJson: string,
    onEvent: (json: string) => void,
    onClosed: (error: string | null) => void,
) => number;
declare const __twk_sse_close: (token: number) => void;

const CONNECTING = 0;
const OPEN = 1;
const CLOSED = 2;

class EventSourcePolyfill {
    static readonly CONNECTING = CONNECTING;
    static readonly OPEN = OPEN;
    static readonly CLOSED = CLOSED;

    readonly CONNECTING = CONNECTING;
    readonly OPEN = OPEN;
    readonly CLOSED = CLOSED;

    readonly url: string;
    readonly withCredentials = false;
    readyState: number = CONNECTING;

    onopen: ((event: any) => void) | null = null;
    onmessage: ((event: any) => void) | null = null;
    onerror: ((event: any) => void) | null = null;

    private token = 0;
    private listeners = new Map<string, ((event: any) => void)[]>();

    constructor(url: string, init?: { headers?: Record<string, string> }) {
        this.url = String(url);

        const open = (globalThis as any).__twk_sse_open;
        if (typeof open !== 'function') {
            // No SSE delegate: fail like a connection error rather than hang.
            this.readyState = CLOSED;
            setTimeout(() => this.dispatch('error', { type: 'error', message: 'no SSE delegate' }), 0);
            return;
        }

        this.token = open(
            this.url,
            JSON.stringify(init?.headers ?? {}),
            (json: string) => this.onHostEvent(json),
            (error: string | null) => this.onHostClosed(error),
        );
    }

    private onHostEvent(json: string): void {
        if (this.readyState === CLOSED) {
            return;
        }
        if (this.readyState === CONNECTING) {
            // First event doubles as the open signal (the host does not report
            // headers separately).
            this.readyState = OPEN;
            this.dispatch('open', { type: 'open' });
        }

        let frame: { data?: string; event?: string; id?: string } = {};
        try {
            frame = JSON.parse(json);
        } catch {
            frame = { data: json }; // tolerate a host that sends raw data
        }

        const type = frame.event || 'message';
        this.dispatch(type, {
            type,
            data: frame.data ?? '',
            lastEventId: frame.id ?? '',
            origin: this.url,
        });
    }

    private onHostClosed(error: string | null): void {
        if (this.readyState === CLOSED) {
            return;
        }
        this.readyState = CLOSED;
        // EventSource reports both failures and server-side end as 'error'.
        this.dispatch('error', { type: 'error', message: error ?? undefined });
    }

    private dispatch(type: string, event: any): void {
        const handler = (this as any)[`on${type}`];
        if (typeof handler === 'function') {
            try {
                handler.call(this, event);
            } catch {
                /* a listener must not break the stream */
            }
        }
        for (const listener of this.listeners.get(type)?.slice() ?? []) {
            try {
                listener.call(this, event);
            } catch {
                /* ditto */
            }
        }
    }

    addEventListener(type: string, listener: (event: any) => void): void {
        const list = this.listeners.get(type) ?? [];
        list.push(listener);
        this.listeners.set(type, list);
    }

    removeEventListener(type: string, listener: (event: any) => void): void {
        const list = this.listeners.get(type);
        if (!list) return;
        const index = list.indexOf(listener);
        if (index >= 0) list.splice(index, 1);
    }

    close(): void {
        if (this.readyState === CLOSED) {
            return;
        }
        this.readyState = CLOSED;
        const close = (globalThis as any).__twk_sse_close;
        if (typeof close === 'function' && this.token) {
            close(this.token);
        }
    }
}

const globals = globalThis as any;
globals.EventSource = globals.EventSource ?? EventSourcePolyfill;

export {};
