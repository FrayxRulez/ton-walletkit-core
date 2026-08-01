//
// Copyright (c) Fela Ameghino 2026
//
// Distributed under the MIT License. (See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT)
//

/**
 * walletkit StorageAdapter backed by the host's storage delegate
 * (PasswordVault / Keychain / Keystore, or the reference host's file store).
 *
 * The kit-android/kit-ios analog of SwiftStorageAdapter: all four operations are
 * awaited by walletkit, so each one waits for the host to acknowledge — a write
 * must not resolve before it is actually persisted.
 */

/* eslint-disable @typescript-eslint/no-explicit-any */

const OP_GET = 0;
const OP_SET = 1;
const OP_REMOVE = 2;
const OP_CLEAR = 3;

type StoragePrimitive = (
    op: number,
    key: string,
    value: string | null,
    cb: (value: string | null) => void,
) => void;

function primitive(): StoragePrimitive | undefined {
    return (globalThis as any).__twk_storage;
}

export class HostStorageAdapter {
    private readonly prefix: string;

    constructor(config: { prefix?: string } = {}) {
        this.prefix = config.prefix || '';
    }

    /** True when the host exposes a storage delegate. */
    static isAvailable(): boolean {
        return typeof primitive() === 'function';
    }

    private run(op: number, key: string, value: string | null): Promise<string | null> {
        const fn = primitive();
        if (!fn) {
            return Promise.resolve(null);
        }
        return new Promise((resolve) => fn(op, key, value, resolve));
    }

    async get(key: string): Promise<string | null> {
        return this.run(OP_GET, this.prefix + key, null);
    }

    async set(key: string, value: string): Promise<void> {
        await this.run(OP_SET, this.prefix + key, value);
    }

    async remove(key: string): Promise<void> {
        await this.run(OP_REMOVE, this.prefix + key, null);
    }

    async clear(): Promise<void> {
        await this.run(OP_CLEAR, this.prefix, null);
    }
}
