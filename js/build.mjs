//
// Copyright (c) Fela Ameghino 2026
//
// Distributed under the MIT License. (See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT)
//
// Build the JS bundle that the native core runs in QuickJS.
//
// Mirrors the kit-ios build (walletkit-ios-bridge/build.js): same expo-crypto /
// pbkdf2 aliases and es2020-ish target, but emitted as a single IIFE so it can be
// embedded as one blob and evaluated by QuickJS (no module loader in the host).
import * as esbuild from 'esbuild';
import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';
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

// Replace @ton/crypto-primitives wholesale: upstream hashes with jssha, which is
// ~49 ms/call for hmac_sha512 in the QuickJS interpreter. Our shim routes to the
// host's native crypto and falls back to the same pure-JS code when absent.
alias['@ton/crypto-primitives'] = path.resolve(__dirname, 'src/polyfills/crypto-primitives.ts');

await esbuild.build({
    entryPoints: [path.resolve(__dirname, 'src/index.ts')],
    outfile: path.resolve(__dirname, 'dist/bundle.js'),
    bundle: true,
    format: 'iife',
    target: 'es2020',
    platform: 'browser',
    // Minified: 2159 KB of source becomes 1069 KB, and 415 KB deflated becomes
    // 338 KB — which is what a download costs, hundreds of millions of times.
    // (An earlier comment here claimed minifying made the bundle larger. It does
    // not; measured both ways.)
    minify: true,
    // …but not at the cost of Function.prototype.name. esbuild renames functions
    // and classes by default, and anything upstream that switches on a
    // constructor name would break silently rather than loudly. 10 KB deflated
    // is a cheap price for not debugging that inside a wallet.
    keepNames: true,
    sourcemap: false,
    legalComments: 'none',
    logLevel: 'info',
    define: { 'process.env.NODE_ENV': '"production"' },
    alias,
});

const bundlePath = path.resolve(__dirname, 'dist/bundle.js');
console.log(`✅ dist/bundle.js built (${fs.statSync(bundlePath).size} bytes)`);

// Emit the C headers the native core embeds: the bundle as base64 in chunked
// string literals. base64 (no escaping) + string literals compile fast on MSVC,
// unlike a multi-MB byte-array initializer, and this runs in Node (ms) rather
// than via CMake string ops.
//
// Two variants, because the native build cannot compress for itself: deflating
// needs zlib, which the target has (Android, every Unix) or has not (Windows).
// CMake includes whichever matches, so neither side has to guess.
const CHUNK = 20000; // well under the MSVC 65535-byte per-literal limit

function emit(file, payload, rawLength, deflated) {
    const b64 = payload.toString('base64');
    const lines = [];
    for (let i = 0; i < b64.length; i += CHUNK) {
        lines.push('"' + b64.slice(i, i + CHUNK) + '"');
    }
    const header =
        '// Generated from bundle.js by build.mjs. Do not edit.\n' +
        `#define TWK_BUNDLE_B64_DEFLATED ${deflated ? 1 : 0}\n` +
        'static const char twk_bundle_b64[] =\n' +
        lines.join('\n') +
        ';\n' +
        `static const unsigned long twk_bundle_b64_len = ${b64.length}ul;\n` +
        `static const unsigned long twk_bundle_b64_raw_len = ${rawLength}ul;\n`;
    fs.writeFileSync(path.resolve(__dirname, file), header);
    return { chunks: lines.length, size: b64.length };
}

const source = fs.readFileSync(bundlePath);
const plain = emit('dist/bundle.h', source, source.length, false);
const squeezed = emit('dist/bundle_z.h', zlib.deflateSync(source, { level: 9 }), source.length, true);

console.log(`✅ dist/bundle.h built (${plain.chunks} chunks, ${plain.size} bytes embedded)`);
console.log(`✅ dist/bundle_z.h built (${squeezed.chunks} chunks, ${squeezed.size} bytes embedded)`);
