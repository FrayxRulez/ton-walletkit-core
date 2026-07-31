// Runs openapi-generator to emit AOT-safe C# models.
//
// generichost + System.Text.Json is deliberate: it emits hand-written converters
// instead of reflection-based serialization, which is what .NET Native (UWP AOT)
// requires. modelNamePrefix TON matches kit-android's Kotlin models.
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import https from 'node:https';
import path from 'node:path';

const VERSION = '7.9.0';
const jar = path.resolve('build', `openapi-generator-cli-${VERSION}.jar`);
const out = path.resolve('../../bindings/csharp/src/Generated');

function download(url, dest) {
    return new Promise((resolve, reject) => {
        const file = fs.createWriteStream(dest);
        const get = (u) => https.get(u, (res) => {
            if (res.statusCode >= 300 && res.statusCode < 400 && res.headers.location) return get(res.headers.location);
            if (res.statusCode !== 200) return reject(new Error(`HTTP ${res.statusCode} for ${u}`));
            res.pipe(file);
            file.on('finish', () => file.close(resolve));
        }).on('error', reject);
        get(url);
    });
}

fs.mkdirSync('build', { recursive: true });
if (!fs.existsSync(jar)) {
    // The npm wrapper needs an ESM-capable Node and breaks on older ones; the jar
    // is invoked directly to avoid that entirely.
    console.log('downloading openapi-generator…');
    await download(
        `https://repo1.maven.org/maven2/org/openapitools/openapi-generator-cli/${VERSION}/openapi-generator-cli-${VERSION}.jar`,
        jar);
}

fs.rmSync(out, { recursive: true, force: true });
execFileSync('java', [
    '-jar', jar, 'generate',
    '-i', 'build/openapi.json',
    // TS unions produce constructs 3.0 validation dislikes but the generator handles.
    '--skip-validate-spec',
    '-g', 'csharp',
    '--library', 'generichost',
    '-o', 'build/csharp',
    '--global-property', 'models,supportingFiles',
    '--additional-properties',
    'targetFramework=netstandard2.0,modelNamePrefix=TON,packageName=Ton.WalletKit.Api,nullableReferenceTypes=false,useDateTimeOffset=false',
], { stdio: 'inherit' });

// Keep only the models + the client support they need.
const src = path.resolve('build/csharp/src/Ton.WalletKit.Api');
fs.mkdirSync(out, { recursive: true });
// Models, plus only the Client files the models actually need. The rest of
// generichost's output is DI/HttpClient scaffolding for a generated API client,
// which we do not use — the transport is the C ABI.
const clientKeep = new Set(['ClientUtils.cs', 'JsonSerializerOptionsProvider.cs', 'Option.cs',
                            'DateTimeJsonConverter.cs', 'DateTimeNullableJsonConverter.cs']);
for (const folder of ['Model', 'Client']) {
    const from = path.join(src, folder);
    if (!fs.existsSync(from)) continue;
    for (const file of fs.readdirSync(from)) {
        if (!file.endsWith('.cs')) continue;
        if (folder === 'Client' && !clientKeep.has(file)) continue;
        fs.copyFileSync(path.join(from, file), path.join(out, file));
    }
}
console.log(`generated ${fs.readdirSync(out).length} files -> ${out}`);
