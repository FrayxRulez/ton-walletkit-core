/**
 * The walletKit surface the native core calls (kit-ios main.ts analog).
 *
 * M1 minimal: just createMnemonic, to prove the canonical @ton/walletkit loads
 * and runs a pure crypto call in QuickJS. The full initWalletKit + method surface
 * (signers, adapters, wallets, TON Connect) grows here in M2/M3, and the native
 * transport calls these methods directly and awaits the returned promises.
 */

/* eslint-disable @typescript-eslint/no-explicit-any */

import {
    CreateTonMnemonic,
    TonWalletKit,
    ApiClientToncenter,
    MemoryStorageAdapter,
    Network,
    Signer,
    WalletV4R2Adapter,
    WalletV5R1Adapter,
    type NetworkAdapters,
} from '@ton/walletkit';
import { hmac_sha512, pbkdf2_sha512, sha256, sha512, getSecureRandomBytes } from '@ton/crypto-primitives';
import { HostStorageAdapter } from './HostStorageAdapter';

/** The kit instance, created by init(). */
let kit: TonWalletKit | undefined;

function requireKit(): TonWalletKit {
    if (!kit) {
        throw new Error('WalletKit not initialized — call init first');
    }
    return kit;
}

/**
 * Object-by-id registry.
 *
 * The C ABI is JSON, so live objects (signers, adapters) are addressed by id.
 * The lookup lives here because every operation ultimately calls a method on a
 * JS object; holding the reference natively would only hand it straight back.
 * Callers must `release(id)` when done — nothing is collected automatically.
 *
 * Note wallets are NOT kept here: walletkit's WalletManager owns them and
 * addresses them by its own walletId (network:address).
 */
const objects = new Map<string, unknown>();
let nextObjectId = 1;

function retain(kind: string, value: unknown): string {
    const id = `${kind}:${nextObjectId++}`;
    objects.set(id, value);
    return id;
}

function resolve<T>(id: string, kind: string): T {
    const value = objects.get(id);
    if (value === undefined) {
        throw new Error(`Unknown ${kind} id: ${id}`);
    }
    return value as T;
}

/** Best-effort read of a getter that may not exist on every implementation. */
function tryCall(target: any, method: string): unknown {
    try {
        return typeof target?.[method] === 'function' ? target[method]() : undefined;
    } catch {
        return undefined;
    }
}

interface AdapterParams {
    chainId?: string;
    /** TON subwallet id (default 0) — not the kit's walletId. */
    walletId?: number;
    workchain?: number;
}

interface AdapterInfo {
    adapterId: string;
    address: unknown;
    publicKey: unknown;
    walletId: unknown;
    network: unknown;
    chainId: string;
}

/**
 * Adapters need the network's API client (they read seqno/state on chain), which
 * is why init() must run first — mirrors kit-ios createV5R1WalletAdapter.
 */
async function createAdapter(
    Adapter: { create: (signer: any, options: any) => Promise<any> },
    signerId: string,
    params: AdapterParams,
): Promise<AdapterInfo> {
    const signer = resolve<any>(signerId, 'signer');
    const network = params.chainId ? Network.custom(params.chainId) : Network.testnet();
    const adapter = await Adapter.create(signer, {
        client: requireKit().getApiClient(network),
        network,
        walletId: params.walletId,
        workchain: params.workchain,
    });
    return {
        adapterId: retain('adapter', adapter),
        address: tryCall(adapter, 'getAddress'),
        publicKey: tryCall(adapter, 'getPublicKey'),
        walletId: tryCall(adapter, 'getWalletId'),
        network: tryCall(adapter, 'getNetwork'),
        chainId: network.chainId,
    };
}

/** The EventSource opened by sseProbe (diagnostics only). */
let sseProbeSource: any;

/**
 * Push an unsolicited update to the host. It surfaces from twk_receive with
 * request_id = 0, because it answers no request.
 */
function emitEvent(type: string, payload: unknown): void {
    const emit = (globalThis as any).__twk_event;
    if (typeof emit === 'function') {
        emit({ type, payload });
    }
}

/**
 * Forward walletkit's callbacks as events (kit-ios setEventsListeners). These are
 * the TON Connect approval requests the host must show the user; respond with the
 * matching approve/reject method, passing the event payload back.
 */
function attachEventListeners(instance: TonWalletKit): void {
    const anyKit = instance as any;
    const wire = (register: string, type: string) => {
        if (typeof anyKit[register] === 'function') {
            anyKit[register](async (event: unknown) => emitEvent(type, event));
        }
    };
    wire('onConnectRequest', 'connectRequest');
    wire('onTransactionRequest', 'transactionRequest');
    wire('onSignDataRequest', 'signDataRequest');
    wire('onSignMessageRequest', 'signMessageRequest');
    wire('onDisconnect', 'disconnect');
}

/** Wallets are owned by walletkit's WalletManager, addressed by its walletId. */
function requireWallet(walletId: string): any {
    const wallet = requireKit().getWallet(walletId);
    if (!wallet) {
        throw new Error(`Unknown walletId: ${walletId}`);
    }
    return wallet;
}

/**
 * The adapter methods (sign, stateInit, …) work on either a retained adapter id
 * or a registered walletId, because a wallet *is* a WalletAdapter — same as
 * kit-ios, where TONWallet conforms to TONWalletAdapterProtocol.
 */
function requireAdapter(id: string): any {
    return objects.has(id) ? objects.get(id) : requireWallet(id);
}

function describeWallet(wallet: any) {
    return {
        walletId: tryCall(wallet, 'getWalletId'),
        address: tryCall(wallet, 'getAddress'),
        publicKey: tryCall(wallet, 'getPublicKey'),
        version: tryCall(wallet, 'getVersion'),
        network: tryCall(wallet, 'getNetwork'),
    };
}

/** Network config accepted by init(); mirrors kit-ios networkConfigurations. */
interface NetworkConfig {
    chainId?: string;
    endpoint?: string;
    apiKey?: string;
    timeout?: number;
}

interface InitConfig {
    networks?: NetworkConfig[];
    walletManifest?: unknown;
    deviceInfo?: unknown;
    /** kit-ios devConfiguration: test hooks, not a boolean. */
    dev?: {
        disableNetworkSend?: boolean;
        disableManifestDomainCheck?: boolean;
    };
    /** Key prefix for host storage (namespaces one account's data). */
    storagePrefix?: string;
    /**
     * TON Connect bridge. Required to answer dapps: approvals are sent back
     * through the relay, so without it approve* fails with "Bridge not
     * initialized for sending response". Mirrors kit-ios configuration.bridge.
     */
    bridge?: {
        bridgeUrl?: string;
        deviceInfo?: unknown;
        walletInfo?: unknown;
        jsBridgeKey?: string;
    };
}

(globalThis as any).walletKit = {
    /**
     * Build the kit (kit-ios initWalletKit analog). Each configured network gets an
     * ApiClientToncenter, whose requests go out through our fetch shim -> the host's
     * http_request delegate (TDLib on Windows, WinHTTP in the reference host).
     * Storage is in-memory for now; the storage delegate lands in M3.
     */
    async initWalletKit(config: InitConfig = {}): Promise<{ networks: string[] }> {
        const networks: NetworkAdapters = {};
        const configs = config.networks?.length ? config.networks : [{ chainId: Network.testnet().chainId }];

        for (const entry of configs) {
            const network = entry.chainId ? Network.custom(entry.chainId) : Network.testnet();
            networks[network.chainId] = {
                apiClient: new ApiClientToncenter({
                    network,
                    endpoint: entry.endpoint,
                    apiKey: entry.apiKey,
                    timeout: entry.timeout,
                }),
            };
        }

        // Persist through the host when it provides a storage delegate; fall back
        // to memory so the kit still runs without one (tests, diagnostics).
        const storage = HostStorageAdapter.isAvailable()
            ? new HostStorageAdapter({ prefix: config.storagePrefix ?? '' })
            : new MemoryStorageAdapter({});

        kit = new TonWalletKit({
            networks,
            storage: storage as any,
            walletManifest: config.walletManifest as any,
            deviceInfo: config.deviceInfo as any,
            bridge: config.bridge as any,
            dev: config.dev,
        });
        await kit.ensureInitialized();
        attachEventListeners(kit);

        return { networks: Object.keys(networks) };
    },

    /** True once initWalletKit has completed (kit-ios isReady). */
    async isReady(): Promise<boolean> {
        return kit !== undefined;
    },

    /**
     * Balance of an arbitrary address, in nanotons, via walletkit's own API client.
     * Deliberately NOT named getBalance: in kit-ios that is a method on a wallet,
     * which this is not — it needs no wallet at all.
     */
    async getAddressBalance(address: string, chainId?: string): Promise<{ address: string; balance: string }> {
        const network = chainId ? Network.custom(chainId) : Network.testnet();
        const client = requireKit().getApiClient(network);
        const balance = await client.getBalance(address);
        return { address, balance: String(balance) };
    },

    /**
     * Account state for any address — no wallet or signer required. This is the
     * watch-only view: balance, status and the last transaction pointer.
     */
    async getAddressState(address: string, chainId?: string): Promise<unknown> {
        const network = chainId ? Network.custom(chainId) : Network.testnet();
        return requireKit().getApiClient(network).getAccountState(address);
    },

    /**
     * Transaction history for any address (watch-only). `limit` is clamped to 100
     * by walletkit.
     */
    async getAddressTransactions(address: string, chainId?: string, limit = 10, offset = 0): Promise<unknown> {
        const network = chainId ? Network.custom(chainId) : Network.testnet();
        return requireKit()
            .getApiClient(network)
            .getAccountTransactions({ address: [address], limit, offset });
    },

    async createMnemonic(): Promise<string[]> {
        return await CreateTonMnemonic();
    },

    // ---- signers ---------------------------------------------------------
    // The secret key stays inside the JS Signer; only its id and public key
    // cross the ABI. walletkit never persists signers, so the host is
    // responsible for storing the mnemonic securely and re-creating the signer.

    async createSignerFromMnemonic(
        mnemonic: string | string[],
        type = 'ton',
    ): Promise<{ signerId: string; publicKey: string }> {
        const signer = await Signer.fromMnemonic(mnemonic as any, { type } as any);
        return { signerId: retain('signer', signer), publicKey: signer.publicKey };
    },

    async createSignerFromPrivateKey(secretKey: string): Promise<{ signerId: string; publicKey: string }> {
        const signer = await Signer.fromPrivateKey(secretKey);
        return { signerId: retain('signer', signer), publicKey: signer.publicKey };
    },

    /** Signs raw bytes with a retained signer (kit-ios signer.sign). */
    async sign(signerId: string, data: number[]): Promise<string> {
        return resolve<any>(signerId, 'signer').sign(Uint8Array.from(data));
    },

    // ---- wallet adapters -------------------------------------------------

    async createV5R1WalletAdapter(signerId: string, params: AdapterParams = {}): Promise<AdapterInfo> {
        return createAdapter(WalletV5R1Adapter, signerId, params);
    },

    async createV4R2WalletAdapter(signerId: string, params: AdapterParams = {}): Promise<AdapterInfo> {
        return createAdapter(WalletV4R2Adapter, signerId, params);
    },

    // ---- wallets ---------------------------------------------------------

    /** Registers an adapter with the kit; the wallet is then addressed by walletId. */
    async addWallet(adapterId: string): Promise<unknown> {
        const adapter = resolve<any>(adapterId, 'adapter');
        const wallet = await requireKit().addWallet(adapter);
        return describeWallet(wallet);
    },

    async getWallets(): Promise<unknown[]> {
        return requireKit()
            .getWallets()
            .map((wallet) => describeWallet(wallet));
    },

    async getWallet(walletId: string): Promise<unknown> {
        const wallet = requireKit().getWallet(walletId);
        return wallet ? describeWallet(wallet) : null;
    },

    async removeWallet(walletId: string): Promise<{ ok: true }> {
        await requireKit().removeWallet(walletId);
        return { ok: true };
    },

    async clearWallets(): Promise<{ ok: true }> {
        await requireKit().clearWallets();
        return { ok: true };
    },

    // ---- adapter operations (kit-ios TONWalletAdapterProtocol) -------------
    // `id` is an adapter id or a walletId: a registered wallet is also an
    // adapter, so both answer these.

    /** Deployment state init, base64 BOC (kit-ios adapter.stateInit()). */
    async getStateInit(id: string): Promise<string> {
        return requireAdapter(id).getStateInit();
    },

    /**
     * Signs a transaction and returns the BOC. `fakeSignature: true` produces a
     * correctly shaped but unusable signature — used for fee estimation and by
     * tests, so no test can accidentally move real funds.
     */
    async getSignedSendTransaction(
        id: string,
        transaction: unknown,
        options: { fakeSignature?: boolean } = {},
    ): Promise<string> {
        return requireAdapter(id).getSignedSendTransaction(transaction, {
            fakeSignature: options?.fakeSignature ?? false,
        });
    },

    /** Signs a transaction as a TON Connect signMessage response. */
    async getSignedSignMessage(id: string, transaction: unknown, options?: unknown): Promise<string> {
        return requireAdapter(id).getSignedSignMessage(transaction, options);
    },

    /** Signs prepared sign-data; returns a hex signature. */
    async getSignedSignData(id: string, input: unknown, options?: unknown): Promise<string> {
        return requireAdapter(id).getSignedSignData(input, options);
    },

    /** Signs a ton_proof message; returns a hex signature. */
    async getSignedTonProof(id: string, input: unknown, options?: unknown): Promise<string> {
        return requireAdapter(id).getSignedTonProof(input, options);
    },

    /** TON Connect features this adapter supports, or null. */
    async getSupportedFeatures(id: string): Promise<unknown> {
        return tryCall(requireAdapter(id), 'getSupportedFeatures') ?? null;
    },

    // ---- wallet operations (kit-ios wallet.* methods, walletId first) ------

    /** Wallet balance in nanotons (kit-ios wallet.balance()). */
    async getBalance(walletId: string): Promise<string> {
        return String(await requireWallet(walletId).getBalance());
    },

    /**
     * Builds an unsigned TON transfer (kit-ios wallet.transferTONTransaction).
     * Fields are walletkit's own: recipientAddress, transferAmount (nanotons),
     * and optionally comment | payload, stateInit, extraCurrency, mode.
     */
    async createTransferTonTransaction(walletId: string, params: unknown): Promise<unknown> {
        return requireWallet(walletId).createTransferTonTransaction(params);
    },

    /** Several TON transfers in one transaction. */
    async createTransferMultiTonTransaction(walletId: string, params: unknown[]): Promise<unknown> {
        return requireWallet(walletId).createTransferMultiTonTransaction(params);
    },

    /** Fee/effect preview for a built transaction. */
    async getTransactionPreview(walletId: string, transaction: unknown, options?: unknown): Promise<unknown> {
        return requireWallet(walletId).getTransactionPreview(transaction, options);
    },

    /** Signs and broadcasts. This spends real funds — never called by tests. */
    async sendTransaction(walletId: string, transaction: unknown): Promise<unknown> {
        return requireWallet(walletId).sendTransaction(transaction);
    },

    // ---- jettons (kit-ios wallet jetton methods) --------------------------

    async createTransferJettonTransaction(walletId: string, params: unknown): Promise<unknown> {
        return requireWallet(walletId).createTransferJettonTransaction(params);
    },

    async getJettonBalance(walletId: string, jettonAddress: string): Promise<string> {
        return String(await requireWallet(walletId).getJettonBalance(jettonAddress));
    },

    async getJettonWalletAddress(walletId: string, jettonAddress: string): Promise<string> {
        return requireWallet(walletId).getJettonWalletAddress(jettonAddress);
    },

    async getJettons(walletId: string, params?: unknown): Promise<unknown> {
        return requireWallet(walletId).getJettons(params);
    },

    // ---- NFTs (kit-ios wallet nft methods) --------------------------------

    async createTransferNftTransaction(walletId: string, params: unknown): Promise<unknown> {
        return requireWallet(walletId).createTransferNftTransaction(params);
    },

    async createTransferNftRawTransaction(walletId: string, params: unknown): Promise<unknown> {
        return requireWallet(walletId).createTransferNftRawTransaction(params);
    },

    async getNfts(walletId: string, params: unknown = {}): Promise<unknown> {
        return requireWallet(walletId).getNfts(params);
    },

    /** The NFT at an address, or null when the wallet does not hold it. */
    async getNft(walletId: string, address: string): Promise<unknown> {
        return (await requireWallet(walletId).getNft(address)) ?? null;
    },

    // ---- TON Connect (kit-ios names; events arrive as unsolicited updates) --

    async handleTonConnectUrl(url: string): Promise<unknown> {
        return requireKit().handleTonConnectUrl(url);
    },

    async getSessions(): Promise<unknown> {
        return requireKit().listSessions();
    },

    async disconnect(sessionId: string): Promise<{ ok: true }> {
        await requireKit().disconnect(sessionId);
        return { ok: true };
    },

    // Approve/reject take the event payload delivered by the corresponding event.
    async approveConnectRequest(event: any, response?: any): Promise<unknown> {
        return requireKit().approveConnectRequest(event, response);
    },
    async rejectConnectRequest(event: any, reason?: string): Promise<unknown> {
        return requireKit().rejectConnectRequest(event, reason);
    },
    async approveTransactionRequest(event: any, response?: any): Promise<unknown> {
        return requireKit().approveTransactionRequest(event, response);
    },
    async rejectTransactionRequest(event: any, reason?: string): Promise<unknown> {
        return requireKit().rejectTransactionRequest(event, reason);
    },
    async approveSignDataRequest(event: any, response?: any): Promise<unknown> {
        return requireKit().approveSignDataRequest(event, response);
    },
    async rejectSignDataRequest(event: any, reason?: string): Promise<unknown> {
        return requireKit().rejectSignDataRequest(event, reason);
    },
    async approveSignMessageRequest(event: any, response?: any): Promise<unknown> {
        return requireKit().approveSignMessageRequest(event, response);
    },
    async rejectSignMessageRequest(event: any, reason?: string): Promise<unknown> {
        return requireKit().rejectSignMessageRequest(event, reason);
    },

    /**
     * Opens an EventSource and forwards what it receives as unsolicited updates
     * (type "sse"), so the SSE path can be exercised without a dapp. Resolves once
     * the stream is open; the caller closes it with sseProbeClose.
     */
    async sseProbe(url: string): Promise<{ opened: true }> {
        const source: any = new (globalThis as any).EventSource(url);
        sseProbeSource = source;
        source.onmessage = (event: any) =>
            emitEvent('sse', { kind: 'message', data: event.data, lastEventId: event.lastEventId });
        source.addEventListener('custom', (event: any) => emitEvent('sse', { kind: 'custom', data: event.data }));
        source.onerror = (event: any) => emitEvent('sse', { kind: 'error', message: event.message ?? null });
        source.onopen = () => emitEvent('sse', { kind: 'open' });
        return { opened: true };
    },

    async sseProbeClose(): Promise<{ readyState: number }> {
        sseProbeSource?.close();
        return { readyState: sseProbeSource?.readyState ?? 2 };
    },

    /**
     * Test hook: spins forever. Exists to prove the interrupt watchdog can stop a
     * runaway script — without it this would wedge the worker thread and the
     * client could never be destroyed.
     */
    async spinForever(): Promise<never> {
        // eslint-disable-next-line no-constant-condition
        while (true) {
            /* deliberately empty */
        }
    },

    /** Test hook: pushes an event without a dapp, to exercise the update path. */
    async emitTestEvent(type: string, payload: unknown): Promise<{ ok: true }> {
        emitEvent(type, payload);
        return { ok: true };
    },

    /** Drops a retained signer/adapter. Ids are not collected automatically. */
    async release(id: string): Promise<{ released: boolean }> {
        return { released: objects.delete(id) };
    },

    // Times each crypto primitive the mnemonic path uses, to show which ones are
    // worth backing with native shims. Returns { name: ms-per-call }.
    async benchCrypto(rounds = 50): Promise<Record<string, number>> {
        const data = Buffer.from('the quick brown fox jumps over the lazy dog'.repeat(4), 'utf-8');
        const out: Record<string, number> = {};

        const time = async (name: string, fn: () => Promise<unknown>) => {
            const t0 = Date.now();
            for (let i = 0; i < rounds; i++) {
                await fn();
            }
            out[name] = (Date.now() - t0) / rounds;
        };

        await time('sha256', () => sha256(data));
        await time('sha512', () => sha512(data));
        await time('hmac_sha512', () => hmac_sha512(data, data));
        await time('pbkdf2_390', () => pbkdf2_sha512(data, 'TON seed version', 390, 64));
        await time('pbkdf2_1', () => pbkdf2_sha512(data, 'TON fast seed version', 1, 64));
        await time('randomBytes32', async () => getSecureRandomBytes(32));
        return out;
    },

    // Exercises the fetch shim end to end (used by the fetch tests): performs a
    // real request through the host delegate and reports what came back.
    async httpProbe(url: string, options?: { method?: string; body?: string; timeoutMs?: number }): Promise<unknown> {
        const method = options?.method ?? 'GET';
        const timeoutMs = options?.timeoutMs ?? 0;

        const init: any = { method, headers: { 'x-probe': '1' } };
        if (options?.body != null) {
            init.body = options.body;
            init.headers['content-type'] = 'application/json';
        }

        let timer: any;
        if (timeoutMs > 0) {
            const controller = new AbortController();
            init.signal = controller.signal;
            timer = setTimeout(() => controller.abort(), timeoutMs);
        }

        try {
            const response = await fetch(url, init);
            const text = await response.text();
            return {
                ok: response.ok,
                status: response.status,
                contentType: response.headers.get('content-type'),
                body: text,
            };
        } catch (error: any) {
            return { failed: true, name: error?.name ?? 'Error', message: String(error?.message ?? error) };
        } finally {
            if (timer) clearTimeout(timer);
        }
    },

    /** Round-trips the host storage delegate (used by the storage contract test). */
    async storageProbe(): Promise<unknown> {
        const store = new HostStorageAdapter({ prefix: 'probe:' });
        const missing = await store.get('absent');
        await store.set('k', 'v1');
        const first = await store.get('k');
        await store.set('k', 'v2');
        const updated = await store.get('k');
        await store.remove('k');
        const removed = await store.get('k');
        await store.set('a', '1');
        await store.clear();
        const afterClear = await store.get('a');
        return {
            available: HostStorageAdapter.isAvailable(),
            missing,
            first,
            updated,
            removed,
            afterClear,
        };
    },

    /** Reads a key written by an earlier client (persistence check). */
    async storageGet(key: string): Promise<{ value: string | null }> {
        return { value: await new HostStorageAdapter({}).get(key) };
    },

    /** Writes a key so a later client can read it back. */
    async storageSet(key: string, value: string): Promise<{ ok: true }> {
        await new HostStorageAdapter({}).set(key, value);
        return { ok: true };
    },

    // Diagnostic helpers used by the transport tests (harmless in production).
    async echo(value: unknown): Promise<unknown> {
        return value ?? null;
    },
    async fail(): Promise<never> {
        throw new Error('boom');
    },
};

// Signal to the host that the bundle finished loading and walletKit is ready.
if (typeof (globalThis as any).__twk_ready === 'function') {
    (globalThis as any).__twk_ready();
}
