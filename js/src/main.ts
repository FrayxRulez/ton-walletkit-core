/**
 * The walletKit surface the native core calls (kit-ios main.ts analog).
 *
 * M1 minimal: just createMnemonic, to prove the canonical @ton/walletkit loads
 * and runs a pure crypto call in QuickJS. The full initWalletKit + method surface
 * (signers, adapters, wallets, TON Connect) grows here in M2/M3, and the native
 * transport calls these methods directly and awaits the returned promises.
 */

/* eslint-disable @typescript-eslint/no-explicit-any */

import { CreateTonMnemonic } from '@ton/walletkit';

(globalThis as any).walletKit = {
    async createMnemonic(): Promise<string[]> {
        return await CreateTonMnemonic();
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
