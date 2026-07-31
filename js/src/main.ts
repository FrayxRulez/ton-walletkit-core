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
    type NetworkAdapters,
} from '@ton/walletkit';
import { hmac_sha512, pbkdf2_sha512, sha256, sha512, getSecureRandomBytes } from '@ton/crypto-primitives';

/** The kit instance, created by init(). */
let kit: TonWalletKit | undefined;

function requireKit(): TonWalletKit {
    if (!kit) {
        throw new Error('WalletKit not initialized — call init first');
    }
    return kit;
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
    dev?: boolean;
}

(globalThis as any).walletKit = {
    /**
     * Build the kit (kit-ios initWalletKit analog). Each configured network gets an
     * ApiClientToncenter, whose requests go out through our fetch shim -> the host's
     * http_request delegate (TDLib on Windows, WinHTTP in the reference host).
     * Storage is in-memory for now; the storage delegate lands in M3.
     */
    async init(config: InitConfig = {}): Promise<{ networks: string[] }> {
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

        kit = new TonWalletKit({
            networks,
            storage: new MemoryStorageAdapter({}),
            walletManifest: config.walletManifest as any,
            deviceInfo: config.deviceInfo as any,
            dev: config.dev,
        });
        await kit.ensureInitialized();

        return { networks: Object.keys(networks) };
    },

    /**
     * Balance of an arbitrary address, in nanotons, via walletkit's own API client
     * (no wallet required — wallets arrive in M3).
     */
    async getBalance(address: string, chainId?: string): Promise<{ address: string; balance: string }> {
        const network = chainId ? Network.custom(chainId) : Network.testnet();
        const client = requireKit().getApiClient(network);
        const balance = await client.getBalance(address);
        return { address, balance: String(balance) };
    },

    async createMnemonic(): Promise<string[]> {
        return await CreateTonMnemonic();
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
