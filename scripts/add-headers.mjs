// Adds the license header to every source file that should carry one.
//
//   node scripts/add-headers.mjs          add what is missing
//   node scripts/add-headers.mjs --check  report what is missing, change nothing
//
// Idempotent, so it can be re-run after adding files. The file list comes from
// git, which keeps ignored build output and untracked scratch directories out of
// it for free; what remains to exclude is vendored code and dependency trees.
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import { markerFor, hasHeader, looksGenerated, withHeader } from './license-header.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');

// Code we did not write. Generated output is skipped too, but by content — see
// looksGenerated.
const EXCLUDE = [
    /(^|\/)node_modules\//,
    /^third_party\//,
    /^js\/dist\//,
    /(^|\/)\.upstream\//,
];

const check = process.argv.includes('--check');

const files = execFileSync('git', ['ls-files'], { cwd: root, encoding: 'utf8' })
    .split('\n')
    .map((line) => line.trim())
    .filter(Boolean)
    .filter((file) => !EXCLUDE.some((pattern) => pattern.test(file)))
    .filter((file) => markerFor(file) !== null);

const missing = [];
let changed = 0;
let generated = 0;

for (const file of files) {
    const full = path.join(root, file);
    if (!fs.existsSync(full)) {
        continue; // deleted but still staged
    }
    const source = fs.readFileSync(full, 'utf8');
    if (looksGenerated(source)) {
        generated++;
        continue;
    }
    if (hasHeader(source)) {
        continue;
    }
    missing.push(file);
    if (!check) {
        fs.writeFileSync(full, withHeader(source, file));
        changed++;
    }
}

if (check) {
    if (missing.length > 0) {
        console.error(`missing a license header (${missing.length}):`);
        for (const file of missing) {
            console.error(`  ${file}`);
        }
        console.error('run `node scripts/add-headers.mjs`');
        process.exit(1);
    }
    console.log(`all ${files.length - generated} files carry a license header (${generated} generated, skipped)`);
} else {
    console.log(`${changed} file(s) updated, ${files.length - changed - generated} already had a header, ` +
                `${generated} generated and skipped`);
}
