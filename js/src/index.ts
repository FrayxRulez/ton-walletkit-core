//
// Copyright (c) Fela Ameghino 2026
//
// Distributed under the MIT License. (See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT)
//

/**
 * Entry point for the ton-walletkit-core JS bundle (runs in QuickJS).
 *
 * Modeled on kit-ios (walletkit-ios-bridge/src/index.ts): the polyfills guard on
 * `window`/globals, so we install `window = globalThis` first and load everything
 * via *dynamic* imports (static imports are hoisted and would run before this).
 */

/* eslint-disable @typescript-eslint/no-explicit-any */

async function bootstrap(): Promise<void> {
    let loading = 'window';
    try {
        // The kit-ios polyfills target a JSCore host that exposes `window` and
        // `self`. `self` is not optional: tweetnacl probes
        //   typeof self !== 'undefined' ? (self.crypto || self.msCrypto) : null
        // to find a PRNG, so without it key generation dies with "no PRNG" — and
        // only once something actually generates keys (TON Connect sessions).
        for (const name of ['window', 'self', 'global'] as const) {
            if (typeof (globalThis as any)[name] === 'undefined') {
                (globalThis as any)[name] = globalThis;
            }
        }

        // Static-literal dynamic imports so esbuild inlines them (a variable
        // specifier would leave a runtime import() the host can't resolve).
        loading = 'textEncoder';
        const { default: textEncoder } = await import('./polyfills/textEncoder');
        textEncoder(globalThis);

        loading = 'buffer';
        await import('./polyfills/buffer');
        loading = 'url';
        await import('./polyfills/url');
        loading = 'generic';
        await import('./polyfills/generic');
        // After generic.ts: it installs an inert AbortController that cannot
        // cancel, so ours must win.
        loading = 'fetch';
        await import('./polyfills/fetch');
        loading = 'eventSource';
        await import('./polyfills/eventSource');

        loading = 'main';
        await import('./main');
    } catch (error) {
        // eslint-disable-next-line no-console
        console.error(`🔍 Error loading ${loading}:`, error instanceof Error ? error.toString() : String(error));
    }
}

void bootstrap();
