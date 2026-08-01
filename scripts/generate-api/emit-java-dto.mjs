// Emits the Java DTOs from build/openapi.json, on org.json.
//
// Replaces openapi-generator's Java target. That target only speaks Gson or
// Jackson, and Telegram Android — the consumer this binding exists for — will
// not take a new dependency. org.json is part of the platform, so models that
// read and write themselves through it cost the app nothing.
//
// It also removes reflection from the DTO layer entirely: each model names its
// own fields, so R8 is free to rename them, and a shrunk build cannot silently
// deserialize a field into null. That is the same reason the C# binding uses
// hand-written converters, arrived at from the opposite constraint (.NET Native
// has no reflection to fall back on).
//
// The schema is regular enough for this to be mechanical:
//   - 156 objects, properties of string/number/boolean/ref/enum/array/map
//   - 14 enums, string- or number-valued
//   - 12 unions, every one discriminated by a literal `type` (or `method`,
//     `result`) property — 6 written as anyOf, 6 as allOf of if/then
//   - 6 aliases of string, which become String rather than a class
import fs from 'node:fs';
import path from 'node:path';

const SPEC = path.resolve('build/openapi.json');
const OUT = path.resolve('../../bindings/android/src/main/java/org/ton/walletkit/api');

const doc = JSON.parse(fs.readFileSync(SPEC, 'utf8'));
const schemas = doc.components.schemas;

// ---- naming ---------------------------------------------------------------

// The C# models are named this way (modelNamePrefix=TON over the same schema
// keys), and api/facade.json refers to those names, so both bindings and the
// facade schema stay in step.
const className = (name) => `TON${name.replace(/[^A-Za-z0-9]/g, '')}`;

const pascal = (name) => name.charAt(0).toUpperCase() + name.slice(1);

// Java keywords a property name could collide with.
const RESERVED = new Set(['abstract', 'assert', 'boolean', 'break', 'byte', 'case', 'catch', 'char', 'class',
    'const', 'continue', 'default', 'do', 'double', 'else', 'enum', 'extends', 'final', 'finally', 'float',
    'for', 'goto', 'if', 'implements', 'import', 'instanceof', 'int', 'interface', 'long', 'native', 'new',
    'package', 'private', 'protected', 'public', 'return', 'short', 'static', 'strictfp', 'super', 'switch',
    'synchronized', 'this', 'throw', 'throws', 'transient', 'try', 'void', 'volatile', 'while']);

/** A wire name turned into a Java identifier: snake_case and dots become camelCase. */
function fieldName(wire) {
    let name = wire.replace(/[^A-Za-z0-9]+(.)?/g, (_, next) => (next ? next.toUpperCase() : ''));
    name = name.charAt(0).toLowerCase() + name.slice(1);
    if (!/^[A-Za-z_]/.test(name)) {
        name = `_${name}`;
    }
    return RESERVED.has(name) ? `${name}_` : name;
}

/** SCREAMING_SNAKE for an enum constant, from an arbitrary wire value. */
function constantName(value) {
    const text = String(value);
    if (/^-?\d+$/.test(text)) {
        return `_${text.replace('-', 'MINUS_')}`;
    }
    const name = text.replace(/([a-z0-9])([A-Z])/g, '$1_$2').replace(/[^A-Za-z0-9]+/g, '_').toUpperCase();
    return /^[A-Z_]/.test(name) ? name : `_${name}`;
}

// ---- schema classification ------------------------------------------------

const refName = (ref) => ref.replace('#/components/schemas/', '');

/** Schemas that are just a named string: referring to one means String. */
const ALIASES = new Set();
/** Schemas we emit nothing for and treat as Object. */
const OPAQUE = new Set();

for (const [name, schema] of Object.entries(schemas)) {
    if (schema.type === 'string' && !schema.enum) {
        ALIASES.add(name);
    } else if (!schema.type && !schema.enum && !schema.anyOf && !schema.oneOf && !schema.allOf) {
        OPAQUE.add(name); // `Hex` is {"not":{}} — a type nothing can satisfy
    }
}

const isEnum = (schema) => Array.isArray(schema.enum);

/**
 * A union, as one of two spellings:
 *   anyOf: [{properties:{type:{enum:["text"]}, value:{$ref}}}, …]
 *   allOf: [{if:{properties:{type:{enum:["balance"]}}}, then:{$ref}}, …]
 * Both pick a variant by a literal discriminator; they differ in whether the
 * variant is wrapped in {type, value} or is the whole object.
 */
function unionOf(schema) {
    if (Array.isArray(schema.anyOf)) {
        const variants = schema.anyOf.map((member) => {
            const properties = member.properties ?? {};
            const discriminator = Object.entries(properties).find(([, p]) => Array.isArray(p.enum));
            if (!discriminator) return null;
            const [key, property] = discriminator;
            return {
                key,
                value: property.enum[0],
                wrapped: true,
                payload: properties.value,
            };
        });
        return variants.every(Boolean) ? { kind: 'wrapped', variants } : null;
    }

    if (Array.isArray(schema.allOf) && schema.allOf.every((m) => m.if && m.then)) {
        const variants = schema.allOf.map((member) => {
            const properties = member.if.properties ?? {};
            const [key, property] = Object.entries(properties)[0];
            return {
                key,
                value: property.enum[0],
                wrapped: false,
                payload: member.then,
            };
        });
        return { kind: 'flat', variants };
    }

    return null;
}

// ---- the Java type for a property ----------------------------------------

/**
 * Returns { type, read(jsonExpr, key), write(target, key, field), imports }.
 * `read` produces the expression that reads one property; `write` the statement
 * that writes it.
 */
function mapping(property, owner, propertyName) {
    const imports = new Set();

    if (property.$ref) {
        const target = refName(property.$ref);
        if (ALIASES.has(target)) {
            return { type: 'String', read: (k) => `TonApiJson.optString(json, "${k}")`, write: simpleWrite };
        }
        if (OPAQUE.has(target)) {
            return { type: 'Object', read: (k) => `TonApiJson.opt(json, "${k}")`, write: simpleWrite };
        }
        const schema = schemas[target];
        const type = className(target);
        if (isEnum(schema)) {
            return {
                type,
                read: (k) => `${type}.fromValue(TonApiJson.opt(json, "${k}"))`,
                write: (field, k) => `TonApiJson.put(json, "${k}", ${field} == null ? null : ${field}.getValue());`,
            };
        }
        return {
            type,
            read: (k) => `${type}.fromJson(TonApiJson.optObject(json, "${k}"))`,
            write: (field, k) => `TonApiJson.put(json, "${k}", ${field} == null ? null : ${field}.toJson());`,
        };
    }

    if (Array.isArray(property.enum)) {
        // An enum declared inline on a property becomes a nested type, so two
        // models can each have their own `type` without colliding.
        const type = `${pascal(fieldName(propertyName))}Enum`;
        return {
            type,
            nested: { name: type, values: property.enum, numeric: property.enum.some((v) => typeof v === 'number') },
            read: (k) => `${type}.fromValue(TonApiJson.opt(json, "${k}"))`,
            write: (field, k) => `TonApiJson.put(json, "${k}", ${field} == null ? null : ${field}.getValue());`,
        };
    }

    if (property.type === 'array') {
        const items = property.items ?? {};
        if (items.$ref && !ALIASES.has(refName(items.$ref)) && !OPAQUE.has(refName(items.$ref))) {
            const target = refName(items.$ref);
            const element = className(target);
            imports.add('java.util.List');
            if (isEnum(schemas[target])) {
                return {
                    type: `List<${element}>`,
                    read: (k) => `TonApiJson.enumList(TonApiJson.optArray(json, "${k}"), ${element}.VALUES)`,
                    write: (field, k) => enumListWrite(field, k, element),
                };
            }
            return {
                type: `List<${element}>`,
                read: (k) => `TonApiJson.list(TonApiJson.optArray(json, "${k}"), ${element}.PARSER)`,
                write: (field, k) => listWrite(field, k, element),
            };
        }
        imports.add('java.util.List');
        if (items.type === 'string' || (items.$ref && ALIASES.has(refName(items.$ref)))) {
            return {
                type: 'List<String>',
                read: (k) => `TonApiJson.stringList(TonApiJson.optArray(json, "${k}"))`,
                write: (field, k) => `TonApiJson.put(json, "${k}", TonApiJson.array(${field}));`,
            };
        }
        return {
            type: 'List<Object>',
            read: (k) => `TonApiJson.anyList(TonApiJson.optArray(json, "${k}"))`,
            write: (field, k) => `TonApiJson.put(json, "${k}", TonApiJson.array(${field}));`,
        };
    }

    if (property.type === 'object' && property.additionalProperties) {
        imports.add('java.util.Map');
        const additional = property.additionalProperties;
        if (additional.$ref && !ALIASES.has(refName(additional.$ref))) {
            const element = className(refName(additional.$ref));
            return {
                type: `Map<String, ${element}>`,
                read: (k) => `TonApiJson.map(TonApiJson.optObject(json, "${k}"), ${element}.PARSER)`,
                write: (field, k) => mapWrite(field, k, element),
            };
        }
        if (additional.$ref || additional.type === 'string') {
            return {
                type: 'Map<String, String>',
                read: (k) => `TonApiJson.stringMap(TonApiJson.optObject(json, "${k}"))`,
                write: (field, k) => `TonApiJson.put(json, "${k}", TonApiJson.object(${field}));`,
            };
        }
        return {
            type: 'Map<String, Object>',
            read: (k) => `TonApiJson.anyMap(TonApiJson.optObject(json, "${k}"))`,
            write: (field, k) => `TonApiJson.put(json, "${k}", TonApiJson.object(${field}));`,
        };
    }

    switch (property.type) {
        case 'string':
            return { type: 'String', read: (k) => `TonApiJson.optString(json, "${k}")`, write: simpleWrite };
        case 'boolean':
            return { type: 'Boolean', read: (k) => `TonApiJson.optBoolean(json, "${k}")`, write: simpleWrite };
        case 'number':
        case 'integer':
            // One numeric type: JSON has one, and every consumer of these fields
            // is either display or arithmetic that JS already did in double.
            return { type: 'Double', read: (k) => `TonApiJson.optDouble(json, "${k}")`, write: simpleWrite };
        default:
            return { type: 'Object', read: (k) => `TonApiJson.opt(json, "${k}")`, write: simpleWrite };
    }
}

const simpleWrite = (field, key) => `TonApiJson.put(json, "${key}", ${field});`;

const listWrite = (field, key, element) => `if (${field} != null) {
            JSONArray array = new JSONArray();
            for (${element} item : ${field}) {
                array.put(item == null ? JSONObject.NULL : item.toJson());
            }
            TonApiJson.put(json, "${key}", array);
        }`;

const enumListWrite = (field, key, element) => `if (${field} != null) {
            JSONArray array = new JSONArray();
            for (${element} item : ${field}) {
                array.put(item == null ? JSONObject.NULL : item.getValue());
            }
            TonApiJson.put(json, "${key}", array);
        }`;

const mapWrite = (field, key, element) => `if (${field} != null) {
            JSONObject nested = new JSONObject();
            for (java.util.Map.Entry<String, ${element}> entry : ${field}.entrySet()) {
                TonApiJson.put(nested, entry.getKey(), entry.getValue() == null ? null : entry.getValue().toJson());
            }
            TonApiJson.put(json, "${key}", nested);
        }`;

// ---- emitters -------------------------------------------------------------

const HEADER = `//
// Copyright (c) Fela Ameghino 2026
//
// Distributed under the MIT License. (See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT)
//
// GENERATED by scripts/generate-api/emit-java-dto.mjs from the walletkit types —
// do not edit. Run \`npm run java\` in scripts/generate-api to regenerate.
//

package org.ton.walletkit.api;
`;

function docComment(description, indent = 0) {
    if (!description) return '';
    // The upstream types carry TonTech's license notice in their doc comments;
    // it belongs in THIRD_PARTY_NOTICES, not on 156 Java classes.
    const text = String(description).replace(/Copyright \(c\) TonTech[\s\S]*?source tree\./g, '').trim();
    if (!text) return '';
    const pad = ' '.repeat(indent);
    const lines = text.split('\n').map((line) => line.trim()).filter(Boolean);
    if (lines.length === 1) {
        return `${pad}/** ${escapeDoc(lines[0])} */\n`;
    }
    return `${pad}/**\n${lines.map((line) => `${pad} * ${escapeDoc(line)}`).join('\n')}\n${pad} */\n`;
}

const escapeDoc = (text) => text.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
    .replace(/@/g, '&#64;').replace(/\*\//g, '*&#47;');

function emitEnum(name, schema) {
    const type = className(name);
    const numeric = schema.enum.some((value) => typeof value === 'number');
    const boxed = numeric ? 'Double' : 'String';

    let out = HEADER + '\n';
    out += docComment(schema.description);
    out += `public enum ${type} {\n\n`;
    out += schema.enum
        .map((value) => `    ${constantName(value)}(${numeric ? `${Number(value)}d` : JSON.stringify(value)})`)
        .join(',\n') + ';\n\n';
    out += `    /** Reads one of these from a wire value; for arrays of this enum. */\n`;
    out += `    public static final EnumParser<${type}> VALUES = new EnumParser<${type}>() {\n`;
    out += `        @Override\n        public ${type} parse(Object wire) {\n            return fromValue(wire);\n        }\n    };\n\n`;
    out += `    private final ${boxed} value;\n\n`;
    out += `    ${type}(${boxed} value) {\n        this.value = value;\n    }\n\n`;
    out += `    public ${boxed} getValue() {\n        return value;\n    }\n\n`;
    out += `    @Override\n    public String toString() {\n        return String.valueOf(value);\n    }\n\n`;
    out += `    /** The constant for a wire value, or null if it is absent or unknown. */\n`;
    out += `    public static ${type} fromValue(Object wire) {\n`;
    out += `        if (wire == null) {\n            return null;\n        }\n`;
    if (numeric) {
        out += `        double number = wire instanceof Number ? ((Number) wire).doubleValue()\n`;
        out += `                : Double.parseDouble(wire.toString());\n`;
        out += `        for (${type} candidate : values()) {\n`;
        out += `            if (candidate.value.doubleValue() == number) {\n                return candidate;\n            }\n        }\n`;
    } else {
        out += `        String text = wire.toString();\n`;
        out += `        for (${type} candidate : values()) {\n`;
        out += `            if (candidate.value.equals(text)) {\n                return candidate;\n            }\n        }\n`;
    }
    // Unknown values are dropped rather than thrown on: walletkit adds enum
    // members without a major version, and a hard failure here would break a
    // whole response over one field the caller may not even read.
    out += `        return null;\n    }\n}\n`;
    return out;
}

function emitNestedEnum(nested, indent = 4) {
    const pad = ' '.repeat(indent);
    const boxed = nested.numeric ? 'Double' : 'String';
    let out = `${pad}/** Values this property accepts. */\n`;
    out += `${pad}public enum ${nested.name} {\n\n`;
    out += nested.values
        .map((value) => `${pad}    ${constantName(value)}(${nested.numeric ? `${Number(value)}d` : JSON.stringify(value)})`)
        .join(',\n') + ';\n\n';
    out += `${pad}    private final ${boxed} value;\n\n`;
    out += `${pad}    ${nested.name}(${boxed} value) {\n${pad}        this.value = value;\n${pad}    }\n\n`;
    out += `${pad}    public ${boxed} getValue() {\n${pad}        return value;\n${pad}    }\n\n`;
    out += `${pad}    @Override\n${pad}    public String toString() {\n${pad}        return String.valueOf(value);\n${pad}    }\n\n`;
    out += `${pad}    public static ${nested.name} fromValue(Object wire) {\n`;
    out += `${pad}        if (wire == null) {\n${pad}            return null;\n${pad}        }\n`;
    if (nested.numeric) {
        out += `${pad}        double number = wire instanceof Number ? ((Number) wire).doubleValue()\n`;
        out += `${pad}                : Double.parseDouble(wire.toString());\n`;
        out += `${pad}        for (${nested.name} candidate : values()) {\n`;
        out += `${pad}            if (candidate.value.doubleValue() == number) {\n${pad}                return candidate;\n${pad}            }\n${pad}        }\n`;
    } else {
        out += `${pad}        String text = wire.toString();\n`;
        out += `${pad}        for (${nested.name} candidate : values()) {\n`;
        out += `${pad}            if (candidate.value.equals(text)) {\n${pad}                return candidate;\n${pad}            }\n${pad}        }\n`;
    }
    out += `${pad}        return null;\n${pad}    }\n${pad}}\n`;
    return out;
}

function emitObject(name, schema) {
    const type = className(name);
    const properties = Object.entries(schema.properties ?? {});
    const required = new Set(schema.required ?? []);

    const fields = [];
    const imports = new Set();
    const nested = [];
    let usesJsonArray = false;

    for (const [wire, property] of properties) {
        const map = mapping(property, name, wire);
        // Imports come from the resulting type rather than from what mapping()
        // reports: one branch forgetting to declare them is a compile error in
        // 200 files, and the type name already says everything needed.
        if (map.type.startsWith('List<')) imports.add('java.util.List');
        if (map.type.startsWith('Map<')) imports.add('java.util.Map');
        if (map.nested) nested.push(map.nested);
        const field = fieldName(wire);
        const write = map.write(field, wire);
        if (write.includes('JSONArray')) usesJsonArray = true;
        fields.push({ wire, field, type: map.type, read: map.read(wire), write,
                      doc: property.description, required: required.has(wire) });
    }

    let out = HEADER + '\n';
    const sorted = [...imports].sort();
    if (sorted.length > 0) {
        out += sorted.map((entry) => `import ${entry};\n`).join('');
        out += '\n';
    }
    if (usesJsonArray) {
        out += 'import org.json.JSONArray;\n';
    }
    out += 'import org.json.JSONObject;\n\n';

    out += docComment(schema.description);
    out += `public class ${type} {\n\n`;

    out += `    /** Builds one of these from JSON. */\n`;
    out += `    public static final Parser<${type}> PARSER = new Parser<${type}>() {\n`;
    out += `        @Override\n        public ${type} parse(JSONObject json) {\n            return fromJson(json);\n        }\n    };\n\n`;

    for (const entry of nested) {
        out += emitNestedEnum(entry);
        out += '\n';
    }

    for (const field of fields) {
        out += `    private ${field.type} ${field.field};\n`;
    }
    out += fields.length > 0 ? '\n' : '';

    for (const field of fields) {
        const accessor = pascal(field.field);
        // One comment, not two: javac treats a second javadoc block before a
        // declaration as dangling.
        const note = field.required ? 'Required by the API.' : '';
        const doc = [field.doc, note].filter(Boolean).join('\n');
        out += docComment(doc, 4);
        out += `    public ${field.type} get${accessor}() {\n        return ${field.field};\n    }\n\n`;
        out += `    public ${type} set${accessor}(${field.type} value) {\n`;
        out += `        this.${field.field} = value;\n        return this;\n    }\n\n`;
    }

    out += `    /** This model as JSON. Null members are omitted, not written as null. */\n`;
    out += `    public JSONObject toJson() {\n        JSONObject json = new JSONObject();\n`;
    for (const field of fields) {
        out += `        ${field.write}\n`;
    }
    out += `        return json;\n    }\n\n`;

    out += `    /** @return null when {@code json} is null. */\n`;
    out += `    public static ${type} fromJson(JSONObject json) {\n`;
    out += `        if (json == null) {\n            return null;\n        }\n`;
    out += `        ${type} out = new ${type}();\n`;
    for (const field of fields) {
        out += `        out.${field.field} = ${field.read};\n`;
    }
    out += `        return out;\n    }\n\n`;

    out += `    @Override\n    public String toString() {\n        return toJson().toString();\n    }\n}\n`;
    return out;
}

function emitUnion(name, schema, union) {
    const type = className(name);
    const key = union.variants[0].key;

    // Each variant's payload type: the wrapped spelling puts it under `value`,
    // the flat one is the object itself.
    const variants = union.variants.map((variant) => {
        const payload = variant.payload;
        const ref = payload?.$ref ? refName(payload.$ref) : null;
        const known = ref && !ALIASES.has(ref) && !OPAQUE.has(ref) && !isEnum(schemas[ref]);
        return {
            ...variant,
            payloadType: known ? className(ref) : payload?.type === 'string' ? 'String' : null,
            accessor: pascal(fieldName(String(variant.value))),
        };
    });

    let out = HEADER + '\nimport org.json.JSONObject;\n\n';
    out += docComment(schema.description);
    out += `/**\n * A union: {@code ${key}} says which variant this is, and the matching\n`;
    out += ` * accessor returns it. An unrecognised ${key} leaves the value as the raw\n`;
    out += ` * JSON rather than failing, so a new variant upstream does not break a\n * response that merely contains one.\n */\n`;
    out += `public class ${type} {\n\n`;

    out += `    public static final Parser<${type}> PARSER = new Parser<${type}>() {\n`;
    out += `        @Override\n        public ${type} parse(JSONObject json) {\n            return fromJson(json);\n        }\n    };\n\n`;

    for (const variant of variants) {
        out += `    public static final String ${constantName(variant.value)} = ${JSON.stringify(String(variant.value))};\n`;
    }
    out += '\n';

    out += `    private String ${fieldName(key)};\n`;
    out += `    private Object value;\n\n`;

    out += `    /** Which variant this is; one of the constants above. */\n`;
    out += `    public String get${pascal(fieldName(key))}() {\n        return ${fieldName(key)};\n    }\n\n`;
    out += `    /** The variant, typed by {@code ${key}}, or the raw JSON if unrecognised. */\n`;
    out += `    public Object getValue() {\n        return value;\n    }\n\n`;

    for (const variant of variants) {
        if (!variant.payloadType) continue;
        out += `    /** The value when {@code ${key}} is {@code ${variant.value}}, else null. */\n`;
        out += `    public ${variant.payloadType} as${variant.accessor}() {\n`;
        out += `        return value instanceof ${variant.payloadType} ? (${variant.payloadType}) value : null;\n    }\n\n`;
    }

    out += `    public JSONObject toJson() {\n`;
    if (union.kind === 'wrapped') {
        out += `        JSONObject json = new JSONObject();\n`;
        out += `        TonApiJson.put(json, "${key}", ${fieldName(key)});\n`;
        out += `        TonApiJson.put(json, "value", asJson(value));\n`;
        out += `        return json;\n    }\n\n`;
    } else {
        // The flat spelling has no wrapper: the variant IS the object, and it
        // already carries the discriminator. asJson returns a fresh object, so
        // adding the discriminator back does not touch the variant.
        out += `        Object encoded = asJson(value);\n`;
        out += `        JSONObject json = encoded instanceof JSONObject ? (JSONObject) encoded : new JSONObject();\n`;
        out += `        TonApiJson.put(json, "${key}", ${fieldName(key)});\n`;
        out += `        return json;\n    }\n\n`;
    }

    out += `    private static Object asJson(Object value) {\n`;
    for (const variant of variants) {
        if (!variant.payloadType || variant.payloadType === 'String') continue;
        out += `        if (value instanceof ${variant.payloadType}) {\n`;
        out += `            return ((${variant.payloadType}) value).toJson();\n        }\n`;
    }
    out += `        return value;\n    }\n\n`;

    out += `    public static ${type} fromJson(JSONObject json) {\n`;
    out += `        if (json == null) {\n            return null;\n        }\n`;
    out += `        ${type} out = new ${type}();\n`;
    out += `        out.${fieldName(key)} = TonApiJson.optString(json, "${key}");\n`;
    const source = union.kind === 'wrapped' ? 'TonApiJson.optObject(json, "value")' : 'json';
    out += `        JSONObject payload = ${source};\n`;
    let first = true;
    for (const variant of variants) {
        const constant = constantName(variant.value);
        const branch = first ? 'if' : '} else if';
        first = false;
        out += `        ${branch} (${constant}.equals(out.${fieldName(key)})) {\n`;
        if (variant.payloadType && variant.payloadType !== 'String') {
            out += `            out.value = ${variant.payloadType}.fromJson(payload);\n`;
        } else if (variant.payloadType === 'String') {
            out += `            out.value = TonApiJson.optString(json, "value");\n`;
        } else {
            out += `            out.value = payload;\n`;
        }
    }
    out += `        } else {\n            out.value = payload;\n        }\n`;
    out += `        return out;\n    }\n\n`;

    out += `    @Override\n    public String toString() {\n        return toJson().toString();\n    }\n}\n`;
    return out;
}

// ---- run ------------------------------------------------------------------

fs.mkdirSync(OUT, { recursive: true });

// Only the generated models are cleared, by the prefix every one of them
// carries. The package also holds hand-written support (Parser, EnumParser,
// TonApiJson) that the generated code compiles against, and wiping the whole
// directory takes those with it.
for (const file of fs.readdirSync(OUT)) {
    if (file.startsWith('TON') && file.endsWith('.java')) {
        fs.rmSync(path.join(OUT, file));
    }
}

let objects = 0;
let enums = 0;
let unions = 0;

for (const [name, schema] of Object.entries(schemas)) {
    if (ALIASES.has(name) || OPAQUE.has(name)) {
        continue; // a named string, or a type nothing satisfies
    }

    let source;
    if (isEnum(schema)) {
        source = emitEnum(name, schema);
        enums++;
    } else {
        const union = unionOf(schema);
        if (union) {
            source = emitUnion(name, schema, union);
            unions++;
        } else {
            source = emitObject(name, schema);
            objects++;
        }
    }
    fs.writeFileSync(path.join(OUT, `${className(name)}.java`), source);
}

console.log(`generated ${objects} models, ${enums} enums, ${unions} unions ` +
            `(${ALIASES.size} string aliases and ${OPAQUE.size} opaque skipped) -> ` +
            path.relative(process.cwd(), OUT));
