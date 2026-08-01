//
// Copyright (c) Fela Ameghino 2026
//
// Distributed under the MIT License. (See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT)
//
// Tracks the upstream this library mirrors.
//
//   node scripts/upstream.mjs                 what changed since the pins
//   node scripts/upstream.mjs diff kit-ios    the diff itself, for the paths we mirror
//   node scripts/upstream.mjs files kit       which files changed
//   node scripts/upstream.mjs pin kit-ios     move the pin to the current head
//
// Clones are kept out of the repo, under .upstream/ (gitignored), and are
// blobless partial clones: full history for log/diff, blobs fetched on demand.
// A shallow clone would be smaller but cannot diff two arbitrary commits, which
// is the whole point.
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const pinsFile = path.join(root, 'upstream.json');
const cache = process.env.TWK_UPSTREAM_CACHE ?? path.join(root, '.upstream');

const pins = JSON.parse(fs.readFileSync(pinsFile, 'utf8'));

const [command = 'status', target] = process.argv.slice(2);
const names = target ? [target] : Object.keys(pins.repositories);

for (const name of names) {
    if (!pins.repositories[name]) {
        console.error(`unknown repository: ${name} (have: ${Object.keys(pins.repositories).join(', ')})`);
        process.exit(2);
    }
}

function git(repo, args, options = {}) {
    return execFileSync('git', ['-C', repo, ...args], {
        encoding: 'utf8',
        maxBuffer: 256 * 1024 * 1024,
        ...options,
    });
}

/** Clones on first use, fetches after. Returns the working copy path. */
function checkout(name, entry) {
    const repo = path.join(cache, name);
    if (!fs.existsSync(path.join(repo, '.git'))) {
        fs.mkdirSync(cache, { recursive: true });
        console.error(`cloning ${entry.url} -> ${path.relative(root, repo)} (blobless, first run only)`);
        execFileSync('git', ['clone', '--filter=blob:none', '--no-checkout', entry.url, repo], { stdio: 'inherit' });
    } else {
        execFileSync('git', ['-C', repo, 'fetch', '--quiet', 'origin', entry.ref], { stdio: 'inherit' });
    }
    return repo;
}

function head(repo, entry) {
    return git(repo, ['rev-parse', `origin/${entry.ref}`]).trim();
}

function short(commit) {
    return commit.slice(0, 10);
}

function status(name, entry) {
    const repo = checkout(name, entry);
    const current = head(repo, entry);

    if (current === entry.commit) {
        console.log(`${name}: up to date (${short(current)}, pinned ${entry.date})`);
        return;
    }

    const range = `${entry.commit}..${current}`;
    const all = git(repo, ['log', '--oneline', range]).trim();
    const mine = git(repo, ['log', '--oneline', range, '--', ...entry.paths]).trim();
    const total = all ? all.split('\n').length : 0;
    const relevant = mine ? mine.split('\n').length : 0;

    console.log(`${name}: ${short(entry.commit)} -> ${short(current)}  ` +
                `${total} commit(s), ${relevant} touching what we mirror`);
    if (relevant > 0) {
        console.log(mine.split('\n').map((line) => `    ${line}`).join('\n'));
        console.log(`  files:  node scripts/upstream.mjs files ${name}`);
        console.log(`  diff:   node scripts/upstream.mjs diff ${name}`);
    }
    console.log(`  once absorbed: node scripts/upstream.mjs pin ${name}`);
}

function show(name, entry, args) {
    const repo = checkout(name, entry);
    const current = head(repo, entry);
    if (current === entry.commit) {
        console.log(`${name}: up to date (${short(current)})`);
        return;
    }
    // Blobs are fetched here rather than at clone time; that is the trade.
    process.stdout.write(git(repo, [...args, `${entry.commit}..${current}`, '--', ...entry.paths]));
}

function pin(name, entry) {
    const repo = checkout(name, entry);
    const current = head(repo, entry);
    if (current === entry.commit) {
        console.log(`${name}: already at ${short(current)}`);
        return;
    }

    const date = git(repo, ['log', '-1', '--format=%cs', current]).trim();
    // Rewritten in place so the file keeps its comments and key order.
    const source = fs.readFileSync(pinsFile, 'utf8');
    const updated = source
        .replace(new RegExp(`("${name}"[\\s\\S]*?"commit": ")[0-9a-f]{40}(")`), `$1${current}$2`)
        .replace(new RegExp(`("${name}"[\\s\\S]*?"date": ")[0-9-]+(")`), `$1${date}$2`);

    if (updated === source) {
        console.error(`${name}: could not rewrite the pin — edit upstream.json by hand`);
        process.exit(1);
    }

    fs.writeFileSync(pinsFile, updated);
    console.log(`${name}: pinned to ${short(current)} (${date})`);
}

for (const name of names) {
    const entry = pins.repositories[name];
    switch (command) {
        case 'status': status(name, entry); break;
        case 'diff': show(name, entry, ['diff']); break;
        case 'files': show(name, entry, ['diff', '--stat']); break;
        case 'pin': pin(name, entry); break;
        default:
            console.error(`usage: node scripts/upstream.mjs [status|diff|files|pin] [${Object.keys(pins.repositories).join('|')}]`);
            process.exit(2);
    }
}

if (command === 'status') {
    console.log(`\nnpm @ton/walletkit: pinned ${pins.npm['@ton/walletkit']} ` +
                `(js/package.json is the one that matters; \`npm view @ton/walletkit version\` for the latest)`);
}
