// Build the JS bundle that the native core runs in QuickJS.
//
// Mirrors the kit-ios build (walletkit-ios-bridge/build.js): same expo-crypto /
// pbkdf2 aliases and es2020-ish target, but emitted as a single IIFE so it can be
// embedded as one blob and evaluated by QuickJS (no module loader in the host).
import * as esbuild from 'esbuild';
import fs from 'node:fs';
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
    minify: false, // (minify made the bundle larger here; keep readable)
    sourcemap: false,
    legalComments: 'none',
    logLevel: 'info',
    define: { 'process.env.NODE_ENV': '"production"' },
    alias,
});

const bundlePath = path.resolve(__dirname, 'dist/bundle.js');
console.log(`✅ dist/bundle.js built (${fs.statSync(bundlePath).size} bytes)`);

// Emit the C header the native core embeds: the bundle as base64 in chunked
// string literals. base64 (no escaping) + string literals compile fast on MSVC,
// unlike a multi-MB byte-array initializer, and this runs in Node (ms) rather
// than via CMake string ops.
const b64 = fs.readFileSync(bundlePath).toString('base64');
const CHUNK = 20000; // well under the MSVC 65535-byte per-literal limit
const lines = [];
for (let i = 0; i < b64.length; i += CHUNK) {
    lines.push('"' + b64.slice(i, i + CHUNK) + '"');
}
const header =
    '// Generated from bundle.js by build.mjs. Do not edit.\n' +
    'static const char twk_bundle_b64[] =\n' +
    lines.join('\n') +
    ';\n' +
    `static const unsigned long twk_bundle_b64_len = ${b64.length}ul;\n`;
fs.writeFileSync(path.resolve(__dirname, 'dist/bundle.h'), header);
console.log(`✅ dist/bundle.h built (${lines.length} chunks)`);
