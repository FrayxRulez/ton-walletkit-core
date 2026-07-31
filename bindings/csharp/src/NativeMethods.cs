//
// P/Invoke declarations for the ton-walletkit-core C ABI (include/twk/twk.h).
//
// Marshalling is manual throughout. The ABI speaks UTF-8 `const char*`, while
// .NET's default string marshalling for DllImport is ANSI — passing `string`
// directly would silently mangle any non-ASCII JSON (addresses, comments, dapp
// names). Everything therefore crosses as byte[]/IntPtr with explicit UTF-8
// conversion, which is also .NET Native (AOT) friendly: no reflection-based
// marshalling of custom types.
//
using System;
using System.Runtime.InteropServices;
using System.Text;

namespace Ton.WalletKit.Native
{
    /// <summary>Opaque handle to a core client (one QuickJS runtime + worker thread).</summary>
    internal static class NativeMethods
    {
        // twk.dll must sit next to the consuming binary (see the packaging task).
        private const string Library = "twk";

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr twk_client_create(IntPtr delegates, IntPtr user);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void twk_client_destroy(IntPtr client);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void twk_send(IntPtr client, ulong requestId, byte[] method, byte[] paramsJson);

        /// <summary>
        /// Returns a pointer owned by the core, valid only until the next receive on
        /// this thread — decode it immediately, on the calling thread.
        /// </summary>
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr twk_receive(IntPtr client, double timeout, out ulong requestId);

        // ---- host -> core completions (safe from any thread) -------------------

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void twk_http_respond(IntPtr client, long token, int status, byte[] headersJson,
                                                     byte[] body);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void twk_http_failed(IntPtr client, long token, byte[] error);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void twk_sse_event(IntPtr client, long token, byte[] data);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void twk_sse_closed(IntPtr client, long token, byte[] error);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void twk_storage_respond(IntPtr client, long token, byte[] value);

        // ---- UTF-8 helpers -----------------------------------------------------

        /// <summary>
        /// UTF-8 encodes with a trailing NUL, since the ABI takes C strings.
        /// Returns null for null so the callee sees a genuine NULL pointer, which
        /// several parameters treat as meaningfully different from "" (a storage
        /// miss, or "no body").
        /// </summary>
        internal static byte[] ToUtf8(string value)
        {
            if (value == null)
            {
                return null;
            }

            int count = Encoding.UTF8.GetByteCount(value);
            var bytes = new byte[count + 1]; // + NUL terminator
            Encoding.UTF8.GetBytes(value, 0, value.Length, bytes, 0);
            return bytes;
        }

        /// <summary>Decodes a NUL-terminated UTF-8 string the core owns.</summary>
        internal static string FromUtf8(IntPtr pointer)
        {
            if (pointer == IntPtr.Zero)
            {
                return null;
            }

            int length = 0;
            while (Marshal.ReadByte(pointer, length) != 0)
            {
                length++;
            }

            if (length == 0)
            {
                return string.Empty;
            }

            var bytes = new byte[length];
            Marshal.Copy(pointer, bytes, 0, length);
            return Encoding.UTF8.GetString(bytes, 0, length);
        }
    }
}
