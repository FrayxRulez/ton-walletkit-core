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

        loading = 'main';
        await import('./main');
    } catch (error) {
        // eslint-disable-next-line no-console
        console.error(`🔍 Error loading ${loading}:`, error instanceof Error ? error.toString() : String(error));
    }
}

void bootstrap();
