//
// Copyright (c) Fela Ameghino 2026
//
// Distributed under the MIT License. (See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT)
//

export default {};

export function getRandomBytes(size) {
    const array = new Uint8Array(size);
    return crypto.getRandomValues(array);
}
