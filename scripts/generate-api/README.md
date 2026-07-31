# generate-api

Regenerates the C# DTOs from `@ton/walletkit`'s own TypeScript types, mirroring
kit-android's codegen so the bindings track upstream instead of drifting by hand.

    npm install
    npm run build      # schema -> openapi -> csharp

Output: `bindings/csharp/src/Generated` (`TON*`-prefixed models).

## Pipeline

1. **`ts-json-schema-generator`** reads `src/api-surface.ts`, which re-exports
   walletkit's `api/models` barrel — the same surface kit-android generates from.
2. **`schema-to-openapi.mjs`** wraps the schema in a minimal OpenAPI 3.0 document.
   It rewrites `const` to a single-value `enum`, because `const` is 3.1-only and
   ts-json-schema-generator emits it for every string-literal type (i.e. most
   discriminators).
3. **`run-openapi-generator.mjs`** downloads the openapi-generator jar and runs
   it. The jar is invoked directly rather than through the npm wrapper, which
   breaks on older Node.

## Pinned versions, and why

- `ts-json-schema-generator@2.4.0` + `typescript@5.9.3` — newer combinations fail
  with "Cannot read InterfaceDeclaration" on walletkit's types.
- `openapi-generator 7.9.0`, `-g csharp --library generichost`.

`generichost` is deliberate: it emits **hand-written System.Text.Json converters**
rather than reflection-based serialization, which is what .NET Native (UWP AOT)
requires. `modelNamePrefix=TON` matches kit-android's Kotlin models.

Only the models and the few converter helpers are copied; the rest of
generichost's output is DI/HttpClient scaffolding for a generated API client,
which we do not use — our transport is the C ABI.

## netstandard2.0 gaps

The consuming project adds `System.Text.Json`, `System.ComponentModel.Annotations`
(for `IValidatableObject`) and `PolySharp` (for the nullable/interop attributes the
generator emits). Without them the generated code does not compile.
