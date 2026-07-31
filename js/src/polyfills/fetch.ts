/**
 * A thin `fetch` for the QuickJS host, built on the native `__twk_http`
 * primitive (which routes to the host's http_request delegate).
 *
 * Scope is deliberately minimal — exactly what `BaseApiClient` uses:
 *   fetch(url, { method, headers, body, signal }) with JSON in/out,
 *   response.ok / .status / .statusText / .headers.get() / .json() / .text(),
 *   and AbortController for the request timeout.
 * No Blob, FormData, streaming, redirects or CORS. walletkit's own
 * ApiClientToncenter keeps doing URL building and parsing in JS, so nothing about
 * the TON API is reimplemented natively.
 */

/* eslint-disable @typescript-eslint/no-explicit-any */

type HttpCallback = (error: string | null, status: number, headersJson: string, body: string) => void;

declare const __twk_http: (
    method: string,
    url: string,
    headersJson: string,
    body: string | null,
    cb: HttpCallback,
) => number;
declare const __twk_http_cancel: (token: number) => void;

class HeadersPolyfill {
    private map = new Map<string, string>();

    constructor(init?: HeadersPolyfill | Record<string, string> | [string, string][]) {
        if (!init) return;
        if (init instanceof HeadersPolyfill) {
            init.forEach((value, key) => this.set(key, value));
        } else if (Array.isArray(init)) {
            for (const [key, value] of init) this.set(key, value);
        } else {
            for (const key of Object.keys(init)) this.set(key, (init as Record<string, string>)[key]);
        }
    }

    // Header names are case-insensitive.
    get(name: string): string | null {
        const value = this.map.get(String(name).toLowerCase());
        return value === undefined ? null : value;
    }
    set(name: string, value: string): void {
        this.map.set(String(name).toLowerCase(), String(value));
    }
    append(name: string, value: string): void {
        const existing = this.get(name);
        this.set(name, existing ? `${existing}, ${value}` : value);
    }
    has(name: string): boolean {
        return this.map.has(String(name).toLowerCase());
    }
    delete(name: string): void {
        this.map.delete(String(name).toLowerCase());
    }
    forEach(fn: (value: string, key: string) => void): void {
        this.map.forEach((value, key) => fn(value, key));
    }
    toJSON(): Record<string, string> {
        const out: Record<string, string> = {};
        this.map.forEach((value, key) => {
            out[key] = value;
        });
        return out;
    }
}

class ResponsePolyfill {
    readonly status: number;
    readonly statusText: string;
    readonly headers: HeadersPolyfill;
    readonly url: string;
    private readonly bodyText: string;

    constructor(body: string, init: { status: number; headers: HeadersPolyfill; url: string }) {
        this.bodyText = body;
        this.status = init.status;
        this.statusText = statusText(init.status);
        this.headers = init.headers;
        this.url = init.url;
    }

    get ok(): boolean {
        return this.status >= 200 && this.status < 300;
    }

    async text(): Promise<string> {
        return this.bodyText;
    }

    async json(): Promise<any> {
        return JSON.parse(this.bodyText);
    }
}

function statusText(status: number): string {
    const known: Record<number, string> = {
        200: 'OK',
        201: 'Created',
        204: 'No Content',
        400: 'Bad Request',
        401: 'Unauthorized',
        403: 'Forbidden',
        404: 'Not Found',
        429: 'Too Many Requests',
        500: 'Internal Server Error',
        502: 'Bad Gateway',
        503: 'Service Unavailable',
    };
    return known[status] ?? '';
}

class AbortSignalPolyfill {
    aborted = false;
    reason: any = undefined;
    private listeners: (() => void)[] = [];

    addEventListener(type: string, listener: () => void): void {
        if (type === 'abort') this.listeners.push(listener);
    }
    removeEventListener(type: string, listener: () => void): void {
        if (type !== 'abort') return;
        const i = this.listeners.indexOf(listener);
        if (i >= 0) this.listeners.splice(i, 1);
    }
    /** @internal */
    _abort(reason: any): void {
        if (this.aborted) return;
        this.aborted = true;
        this.reason = reason;
        for (const listener of this.listeners.slice()) {
            try {
                listener();
            } catch {
                /* a listener must not break abort */
            }
        }
    }
}

class AbortControllerPolyfill {
    readonly signal = new AbortSignalPolyfill();
    abort(reason?: any): void {
        this.signal._abort(reason ?? new Error('The operation was aborted'));
    }
}

function fetchPolyfill(input: any, init: any = {}): Promise<ResponsePolyfill> {
    const url = String(input && input.href ? input.href : input); // accepts URL or string
    const method = (init.method || 'GET').toUpperCase();
    const headers = new HeadersPolyfill(init.headers);
    const body = init.body == null ? null : String(init.body);
    const signal: AbortSignalPolyfill | undefined = init.signal;

    return new Promise((resolve, reject) => {
        if (signal?.aborted) {
            reject(abortError());
            return;
        }

        let settled = false;
        const token = __twk_http(method, url, JSON.stringify(headers.toJSON()), body, (error, status, respHeadersJson, respBody) => {
            if (settled) return;
            settled = true;
            if (signal) signal.removeEventListener('abort', onAbort);

            if (error !== null) {
                reject(new TypeError(`fetch failed: ${error}`));
                return;
            }

            let parsed: Record<string, string> = {};
            try {
                parsed = respHeadersJson ? JSON.parse(respHeadersJson) : {};
            } catch {
                /* tolerate a host that doesn't report headers */
            }
            resolve(new ResponsePolyfill(respBody, { status, headers: new HeadersPolyfill(parsed), url }));
        });

        function onAbort() {
            if (settled) return;
            settled = true;
            __twk_http_cancel(token);
            reject(abortError());
        }

        if (signal) signal.addEventListener('abort', onAbort);
    });
}

function abortError(): Error {
    const error = new Error('The operation was aborted');
    error.name = 'AbortError';
    return error;
}

const globals = globalThis as any;
globals.Headers = globals.Headers ?? HeadersPolyfill;
globals.Response = globals.Response ?? ResponsePolyfill;
globals.fetch = globals.fetch ?? fetchPolyfill;
// Installed unconditionally: the kit-ios generic.ts polyfill provides an inert
// AbortController whose abort() cannot actually cancel anything.
globals.AbortController = AbortControllerPolyfill;
globals.AbortSignal = AbortSignalPolyfill;

export {};
