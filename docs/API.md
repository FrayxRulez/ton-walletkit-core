# Public JSON API

Methods on `globalThis.walletKit`, invoked as `twk_send(client, request_id, method, params_json)`
where `params_json` is a **positional argument array**. Responses are `{"result":…}` or
`{"error":{"message":…}}`; unsolicited events arrive with `request_id = 0`.

Naming mirrors **kit-ios** `window.walletKit`. The one forced deviation: kit-ios passes live
objects (`createV5R1WalletAdapter(signer, …)`, `addWallet(adapter)`); a JSON ABI cannot, so
object arguments become the corresponding **id string** — same method names, same order.
Bindings rebuild the object model on top (`wallet.getBalance()` → `getBalance(walletId)`).

> Shapes below were **verified against live testnet**, not inferred. Where they contradict
> intuition it is called out — several earlier guesses were wrong.

## Lifecycle

| method | args | returns |
|---|---|---|
| `initWalletKit` | `[config?]` | `{networks: string[]}` |
| `isReady` | `[]` | `boolean` |
| `createMnemonic` | `[]` | `string[]` (24 words) |
| `createSignerFromMnemonic` | `[mnemonic, type?]` | `{signerId, publicKey}` |
| `createSignerFromPrivateKey` | `[secretKeyHex]` | `{signerId, publicKey}` |
| `createV5R1WalletAdapter` | `[signerId, params?]` | `{adapterId, address, chainId}` |
| `createV4R2WalletAdapter` | `[signerId, params?]` | `{adapterId, address, chainId}` |
| `addWallet` | `[adapterId]` | wallet info |
| `getWallets` / `getWallet` | `[]` / `[walletId]` | wallet info |
| `removeWallet` / `clearWallets` | `[walletId]` / `[]` | `{ok:true}` |
| `release` | `[id]` | `{released}` — signers/adapters are **not** GC'd; release them |

`initWalletKit` config: `{networks:[{chainId, endpoint?, apiKey?, timeout?}], storagePrefix?, dev?}`.
`createV*WalletAdapter` params: `{chainId?, workchain?, walletId?}` — here `walletId` is the TON
**subwallet id** (default 0), *not* the kit's wallet id. Adapters need the network's API client,
so `initWalletKit` must run first.

## Wallet operations (walletId first)

| method | args | returns |
|---|---|---|
| `getBalance` | `[walletId]` | `{balance}` — nanotons |
| `createTransferTonTransaction` | `[walletId, params]` | `{messages:[…], fromAddress}` |
| `getTransactionPreview` | `[walletId, transaction, options?]` | preview |
| `getSignedSendTransaction` | `[walletId, transaction, {fakeSignature?}]` | `{boc}` |
| `sendTransaction` | `[walletId, transaction]` | `{boc, normalizedBoc, normalizedHash}` — **spends funds** |
| `getAddressBalance` | `[address, chainId?]` | `{address, balance}` — no wallet needed |

`createTransferTonTransaction` params (walletkit's own field names):
`{recipientAddress, transferAmount, comment? | payload?, stateInit?, extraCurrency?, mode?}`.

## TON Connect

| method | args | notes |
|---|---|---|
| `handleTonConnectUrl` | `[tcUrl]` | the connect request is carried **in the URL**; no dapp round-trip needed to raise it |
| `getSessions` / `disconnect` | `[]` / `[sessionId]` | |
| `approveConnectRequest` / `rejectConnectRequest` | `[event, response?]` / `[event, reason?]` | |
| `approveTransactionRequest` / `reject…` | `[event, …]` | same shape for signData / signMessage |

Requests arrive as **unsolicited updates** (`request_id = 0`):
`{"event":{"type":"connectRequest","payload":{…}}}`. Pass that `payload` back to the matching
approve/reject call. Types: `connectRequest`, `transactionRequest`, `signDataRequest`,
`signMessageRequest`, `disconnect`.

Two things are required and are not obvious from the errors:

1. **Set `walletId` on the event before approving** — this is the user choosing which account to
   connect. Without it: `WalletKitError: Wallet is required for embedded request approval`.
2. **`initWalletKit` must be given `bridge` config** (`bridgeUrl`, `deviceInfo`, `walletInfo`,
   `jsBridgeKey`), because the approval is sent back to the dapp through the relay. Without it:
   `Bridge not initialized for sending response`.

## Verified shapes (and corrected assumptions)

- **`walletId` is a base64 hash** (`"pR15i8TgSLrN4Qf+plz+VOdxDPzDxtNaJCAFnGkt+70="`), **not**
  `network:address` — walletkit's own commented-out `storageKey` suggests otherwise.
- **`recipientAddress` must be user-friendly form** (`UQCf…`). Raw `-1:333…` passes
  `isValidAddress` but is then rejected by `validateTransactionMessage`, whose
  `requireFriendlyAddress` defaults to true. The error names the message, not the address, so
  this is easy to misdiagnose.
- Transfer fields are **`recipientAddress` / `transferAmount`**, not `to` / `amount`.
- `publicKey` is **0x-prefixed hex**; `network` is `{chainId}`; testnet is `"-3"`, mainnet `"-239"`.
- `createSignerFromMnemonic` accepts a **string or a word array**.
- `getVersion()` is not implemented on the wallet proxy (returns undefined).

## Persistence — important

**walletkit does not persist wallets or signers.** `WalletManager` keeps them in an in-memory
Map (`// private storageKey = 'wallets'` and `// await this.loadWallets()` are commented out in
the shipped code). The storage delegate holds TON Connect **sessions** (which contain session
private keys), the bridge `lastEventId`, and the event store.

So the host must store the mnemonic/secret itself — deliberately, in a secure store — and
re-create signer → adapter → wallet on startup. Storage must be confidential because session
keys live there; the reference host's plaintext file is **test-only**.

## Safety

`getSignedSendTransaction` with `{"fakeSignature": true}` produces a correctly shaped but
unusable signature: right for fee estimation, and what the tests use so no test can move funds.
Only `sendTransaction` broadcasts.
