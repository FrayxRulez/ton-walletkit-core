// Wraps the JSON Schema from ts-json-schema-generator into a minimal OpenAPI 3.0
// document, which is what openapi-generator consumes. Only components.schemas
// matters — we generate models, not a client.
import fs from 'node:fs';

const schema = JSON.parse(fs.readFileSync('build/schema.json', 'utf8'));
const definitions = schema.definitions ?? schema.$defs ?? {};

// $ref targets must point at components/schemas, and names must be identifier-safe.
// A leading TON is dropped because the generator prefixes every model with TON:
// without this, walletkit's TONTransferRequest becomes TONTONTransferRequest.
// Stripping it here yields kit-ios's own names (TONTransferRequest, TONConnectSession).
const safe = (name) => name.replace(/[^A-Za-z0-9_]/g, '_').replace(/^TON(?=[A-Z])/, '');
const rewrite = (node) => {
    if (Array.isArray(node)) return node.map(rewrite);
    if (node && typeof node === 'object') {
        const out = {};
        for (const [key, value] of Object.entries(node)) {
            if (key === 'const') {
                // JSON Schema / OpenAPI 3.1 only; 3.0 expresses a literal as a
                // single-value enum. ts-json-schema-generator emits const for every
                // string-literal type, which is most discriminators.
                out.enum = [value];
            } else if (key === '$ref' && typeof value === 'string') {
                const target = value.replace(/^#\/(definitions|\$defs)\//, '');
                out.$ref = '#/components/schemas/' + safe(decodeURIComponent(target));
            } else {
                out[key] = rewrite(value);
            }
        }
        return out;
    }
    return node;
};

const schemas = {};
for (const [name, value] of Object.entries(definitions)) {
    schemas[safe(name)] = rewrite(value);
}

const doc = {
    openapi: '3.0.3',
    info: { title: 'TON WalletKit models', version: '1.0.0' },
    paths: {},
    components: { schemas },
};

fs.mkdirSync('build', { recursive: true });
fs.writeFileSync('build/openapi.json', JSON.stringify(doc, null, 2));
console.log(`openapi: ${Object.keys(schemas).length} schemas -> build/openapi.json`);
