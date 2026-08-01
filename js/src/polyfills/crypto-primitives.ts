//
// Copyright (c) Fela Ameghino 2026
//
// Distributed under the MIT License. (See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT)
//

/**
 * Drop-in replacement for `@ton/crypto-primitives` (aliased in build.mjs).
 *
 * The upstream implementations use jssha, which is punishingly slow in the
 * QuickJS interpreter (hmac_sha512 measured ~49 ms/call, and the TON mnemonic
 * loop runs it ~256 times). These route to the host's native crypto
 * (`__twk_crypto`, Uint8Array in/out) and fall back to the pure-JS versions when
 * the host does not publish it (e.g. platforms where the native backend is not
 * wired up yet), so behaviour is identical either way.
 */

/* eslint-disable @typescript-eslint/no-explicit-any */

import jsSHA from 'jssha';

interface NativeCrypto {
    sha256(data: Uint8Array): Uint8Array;
    sha512(data: Uint8Array): Uint8Array;
    hmacSha512(key: Uint8Array, data: Uint8Array): Uint8Array;
    pbkdf2Sha512(password: Uint8Array, salt: Uint8Array, iterations: number, keyLen: number): Uint8Array;
}

const native: NativeCrypto | undefined = (globalThis as any).__twk_crypto;

function toBuffer(src: Buffer | string): Buffer {
    return typeof src === 'string' ? Buffer.from(src, 'utf-8') : src;
}

// jssha fallbacks (upstream's implementations, kept verbatim in behaviour).
function jsHash(variant: 'SHA-256' | 'SHA-512', source: Buffer): Buffer {
    const hasher = new jsSHA(variant, 'HEX');
    hasher.update(source.toString('hex'));
    return Buffer.from(hasher.getHash('HEX'), 'hex');
}

export async function sha256(source: Buffer | string): Promise<Buffer> {
    const data = toBuffer(source);
    if (native) {
        return Buffer.from(native.sha256(new Uint8Array(data.buffer, data.byteOffset, data.byteLength)));
    }
    return jsHash('SHA-256', data);
}

export async function sha512(source: Buffer | string): Promise<Buffer> {
    const data = toBuffer(source);
    if (native) {
        return Buffer.from(native.sha512(new Uint8Array(data.buffer, data.byteOffset, data.byteLength)));
    }
    return jsHash('SHA-512', data);
}

export async function hmac_sha512(key: Buffer | string, data: Buffer | string): Promise<Buffer> {
    const keyBuffer = toBuffer(key);
    const dataBuffer = toBuffer(data);

    if (native) {
        return Buffer.from(
            native.hmacSha512(
                new Uint8Array(keyBuffer.buffer, keyBuffer.byteOffset, keyBuffer.byteLength),
                new Uint8Array(dataBuffer.buffer, dataBuffer.byteOffset, dataBuffer.byteLength),
            ),
        );
    }

    const shaObj = new jsSHA('SHA-512', 'HEX', {
        hmacKey: { value: keyBuffer.toString('hex'), format: 'HEX' },
    });
    shaObj.update(dataBuffer.toString('hex'));
    return Buffer.from(shaObj.getHash('HEX'), 'hex');
}

export async function pbkdf2_sha512(
    key: Buffer | string,
    salt: Buffer | string,
    iterations: number,
    keyLen: number,
): Promise<Buffer> {
    const keyBuffer = toBuffer(key);
    const saltBuffer = toBuffer(salt);

    if (native) {
        return Buffer.from(
            native.pbkdf2Sha512(
                new Uint8Array(keyBuffer.buffer, keyBuffer.byteOffset, keyBuffer.byteLength),
                new Uint8Array(saltBuffer.buffer, saltBuffer.byteOffset, saltBuffer.byteLength),
                iterations,
                keyLen,
            ),
        );
    }

    // Host contract from kit-ios (base64 in/out).
    const pbkdf2 = (globalThis as any).Pbkdf2;
    const res = await pbkdf2.derive(
        keyBuffer.toString('base64'),
        saltBuffer.toString('base64'),
        iterations,
        keyLen,
        'sha-512',
    );
    return Buffer.from(res, 'base64');
}

export function getSecureRandomBytes(size: number): Promise<Buffer> {
    const array = new Uint8Array(size);
    crypto.getRandomValues(array);
    return Promise.resolve(Buffer.from(array));
}

export async function getSecureRandomWords(size: number): Promise<Uint16Array> {
    const bytes = await getSecureRandomBytes(size * 2);
    return new Uint16Array(bytes.buffer, bytes.byteOffset, size);
}
