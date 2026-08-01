// Generates the typed facade for every binding from api/facade.json.
//
//   node scripts/generate-facade.mjs            every language
//   node scripts/generate-facade.mjs csharp     just one
//
// Adding a language means adding one module under scripts/facade/ that turns the
// schema into files — the schema itself never changes for a new binding. See
// docs/BINDINGS.md.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import * as csharp from './facade/csharp.mjs';
import * as java from './facade/java.mjs';

const EMITTERS = { csharp, java };

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const schema = JSON.parse(fs.readFileSync(path.join(root, 'api', 'facade.json'), 'utf8'));

const requested = process.argv.slice(2);
const languages = requested.length > 0 ? requested : Object.keys(EMITTERS);

for (const language of languages) {
    const emitter = EMITTERS[language];
    if (!emitter) {
        console.error(`unknown language: ${language} (have: ${Object.keys(EMITTERS).join(', ')})`);
        process.exit(2);
    }

    const files = emitter.emit(schema);
    for (const file of files) {
        const target = path.join(root, file.path);
        fs.mkdirSync(path.dirname(target), { recursive: true });
        // LF everywhere, as the rest of the repo is, so a regeneration on Windows
        // does not show up as a whole-file diff.
        fs.writeFileSync(target, file.content);
        console.log(`${language}: ${file.path} (${file.content.split('\n').length} lines)`);
    }
}
