// Build the JS bundle that the native core runs in QuickJS.
//
// Mirrors the kit-ios build (walletkit-ios-bridge/build.js): same expo-crypto /
// pbkdf2 aliases and es2020-ish target, but emitted as a single IIFE so it can be
// embedded as one blob and evaluated by QuickJS (no module loader in the host).
import * as esbuild from 'esbuild';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createRequire } from 'node:module';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const require = createRequire(import.meta.url);

const alias = {
    // Randomness comes from the host crypto.getRandomValues shim.
    'expo-crypto': path.resolve(__dirname, 'src/polyfills/expo-crypto.js'),
    // PBKDF2 is provided natively by the host (window.Pbkdf2), like kit-ios.
    'react-native-fast-pbkdf2': path.resolve(__dirname, 'src/polyfills/pbkdf2.js'),
};

// Match kit-ios: use the crypto-primitives "native" build (routes pbkdf2 through
// the aliased react-native-fast-pbkdf2 shim above).
try {
    alias['@ton/crypto-primitives'] = require.resolve('@ton/crypto-primitives/dist/native.js');
} catch {
    // Fall back to the package default if the native subpath is unavailable.
}

await esbuild.build({
    entryPoints: [path.resolve(__dirname, 'src/index.ts')],
    outfile: path.resolve(__dirname, 'dist/bundle.js'),
    bundle: true,
    format: 'iife',
    target: 'es2020',
    platform: 'browser',
    minify: false,
    sourcemap: false,
    legalComments: 'none',
    logLevel: 'info',
    define: { 'process.env.NODE_ENV': '"production"' },
    alias,
});

console.log('✅ dist/bundle.js built');
