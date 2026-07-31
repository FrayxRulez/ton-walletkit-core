# generate-api

Regenerates the C# DTOs from `@ton/walletkit`'s own TypeScript types, mirroring
kit-android's codegen so the bindings track upstream instead of drifting by hand.

    npm install
    npm run build      # surface -> schema -> openapi -> csharp

Output: `bindings/csharp/src/Generated` (`TON*`-prefixed models).

## Pipeline

1. **`build-surface.mjs`** writes `src/api-surface.ts`: one interface member per
   type exported from walletkit's `api/models` barrel (following `export *`
   re-exports). See below for why it is not a one-line re-export.
2. **`ts-json-schema-generator`** turns that surface into JSON Schema. `--expose all`
   gives every referenced type its own definition.
3. **`schema-to-openapi.mjs`** wraps the schema in a minimal OpenAPI 3.0 document.
   It rewrites `const` to a single-value `enum`, because `const` is 3.1-only and
   ts-json-schema-generator emits it for every string-literal type (i.e. most
   discriminators).
4. **`run-openapi-generator.mjs`** downloads the openapi-generator jar, runs it,
   copies out the models, and emits `GeneratedConverters.cs`. The jar is invoked
   directly rather than through the npm wrapper, which breaks on older Node.

## Why the surface is generated

`export type * from '…/api/models'` plus `--type "*"` looks equivalent, and it is
what this used to do — but the generator silently skipped **57** of the exported
types, including most of the request/response shapes the wallet API is made of
(`TONTransferRequest`, `JettonsResponse`, `ConnectionRequestEvent`,
`TONConnectSession`, `PreparedSignData`, `TransactionsResponse`, …). Naming a type
explicitly always works, so the surface names them all. The list comes from
walletkit's own barrel, so upstream additions are picked up on the next run.

A few names are skipped, each for a reason recorded in `build-surface.mjs`:
generic (`Result<T>`), value-not-type (`UnstakeMode`), or a union that the C#
generator cannot express (`TokenAddress`, which is just a string).

## Registering the converters

The generated converters are classes, not attributes, so nothing uses them until
they are added to a `JsonSerializerOptions`. generichost does that in
`HostConfiguration.cs` — DI scaffolding we drop — so the runner lifts its list
into `GeneratedConverters.AddTo(options)`, which `TonJson` calls. Note that
`JsonStringEnumConverter` is deliberately **not** registered: it matches every
enum first and would write the C# name (`"Ton"`) instead of the wire value
(`"ton"`) that the generated per-enum converters know.

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
