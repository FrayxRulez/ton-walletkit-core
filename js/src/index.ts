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
        // The kit-ios polyfills target a JSCore host that exposes `window`.
        if (typeof (globalThis as any).window === 'undefined') {
            (globalThis as any).window = globalThis;
        }

        loading = 'textEncoder';
        const { default: textEncoder } = await import('./polyfills/textEncoder');
        textEncoder(globalThis);

        for (const polyfill of ['./polyfills/buffer', './polyfills/url', './polyfills/generic'] as const) {
            loading = polyfill;
            await import(/* @vite-ignore */ polyfill);
        }

        loading = './main';
        await import('./main');
    } catch (error) {
        // eslint-disable-next-line no-console
        console.error(`🔍 Error loading ${loading}:`, error instanceof Error ? error.toString() : String(error));
    }
}

void bootstrap();
