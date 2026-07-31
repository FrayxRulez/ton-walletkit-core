// Standalone Node smoke for dist/bundle.js: run the bundle with the host globals
// mocked (Pbkdf2 via Node crypto; crypto.getRandomValues is native in Node), then
// call walletKit.createMnemonic() and assert 24 words. Separates "bundle/logic"
// failures from QuickJS-specific ones before we run it in the native core.
import fs from 'node:fs';
import vm from 'node:vm';
import crypto from 'node:crypto';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));

// Host globals the bundle expects (the native core provides these as shims).
globalThis.window = globalThis;
globalThis.Pbkdf2 = {
    derive(passwordB64, saltB64, iterations, keyLen, hash) {
        const algo = hash === 'sha-512' ? 'sha512' : hash.replace('-', '');
        const out = crypto.pbkdf2Sync(
            Buffer.from(passwordB64, 'base64'),
            Buffer.from(saltB64, 'base64'),
            iterations,
            keyLen,
            algo,
        );
        return out.toString('base64');
    },
};

let readyResolve;
const ready = new Promise((res) => {
    readyResolve = res;
});
globalThis.__twk_ready = () => readyResolve();

const code = fs.readFileSync(path.resolve(__dirname, '../dist/bundle.js'), 'utf8');
vm.runInThisContext(code);

const timeout = setTimeout(() => {
    console.error('FAIL: bundle did not signal __twk_ready within 10s');
    process.exit(1);
}, 10_000);
await ready;
clearTimeout(timeout);

const words = await globalThis.walletKit.createMnemonic();
if (!Array.isArray(words) || words.length !== 24) {
    console.error('FAIL: expected 24 words, got', Array.isArray(words) ? words.length : words);
    process.exit(1);
}

console.log(`ok: createMnemonic -> ${words.length} words (${words[0]} ... ${words[23]})`);
process.exit(0);
