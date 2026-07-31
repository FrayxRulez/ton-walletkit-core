//
// Bridges IWalletKitHost to the native twk_delegates block.
//
// Two hazards drive this design:
//
//  1. The GC must not collect or move the callbacks. Marshal.GetFunctionPointerForDelegate
//     does NOT root the delegate, so every delegate instance is held in a field
//     for the object's lifetime. If it were collected the core would call freed
//     memory — a crash that only appears under load, long after the call site.
//
//  2. Callbacks arrive on the core's worker thread and must not block it. Each one
//     starts the async work and returns immediately; completion is reported later
//     via twk_*_respond, which is safe from any thread.
//
using System;
using System.Collections.Concurrent;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using Ton.WalletKit.Native;

namespace Ton.WalletKit
{
    /// <summary>
    /// Owns the unmanaged twk_delegates block and keeps the managed callbacks
    /// alive. Dispose only after the client that uses it has been disposed.
    /// </summary>
    public sealed class WalletKitDelegates : IDisposable
    {
        // Signatures mirror include/twk/twk_delegates.h exactly. Strings arrive as
        // IntPtr so they can be decoded as UTF-8 rather than ANSI.
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate void HttpRequestFn(IntPtr user, IntPtr client, long token, IntPtr method, IntPtr url,
                                            IntPtr headersJson, IntPtr body);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate void TokenFn(IntPtr user, IntPtr client, long token);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate void SseOpenFn(IntPtr user, IntPtr client, long token, IntPtr url, IntPtr headersJson);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate void StorageKeyFn(IntPtr user, IntPtr client, long token, IntPtr key);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate void StorageSetFn(IntPtr user, IntPtr client, long token, IntPtr key, IntPtr value);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate void StorageClearFn(IntPtr user, IntPtr client, long token);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate void LogFn(IntPtr user, IntPtr client, int level, IntPtr message);

        /// <summary>
        /// Layout must match struct twk_delegates field-for-field and in order.
        /// </summary>
        [StructLayout(LayoutKind.Sequential)]
        private struct NativeDelegates
        {
            public IntPtr HttpRequest;
            public IntPtr HttpCancel;
            public IntPtr SseOpen;
            public IntPtr SseClose;
            public IntPtr StorageGet;
            public IntPtr StorageSet;
            public IntPtr StorageRemove;
            public IntPtr StorageClear;
            public IntPtr Log;
        }

        // Rooted for the lifetime of this object — see hazard (1) above.
        private readonly HttpRequestFn _httpRequest;
        private readonly TokenFn _httpCancel;
        private readonly SseOpenFn _sseOpen;
        private readonly TokenFn _sseClose;
        private readonly StorageKeyFn _storageGet;
        private readonly StorageSetFn _storageSet;
        private readonly StorageKeyFn _storageRemove;
        private readonly StorageClearFn _storageClear;
        private readonly LogFn _log;

        private readonly IWalletKitHost _host;
        private readonly IntPtr _block;
        private readonly ConcurrentDictionary<long, CancellationTokenSource> _inFlight =
            new ConcurrentDictionary<long, CancellationTokenSource>();

        private int _disposed;

        /// <summary>Builds the native delegate block for <paramref name="host"/>.</summary>
        public WalletKitDelegates(IWalletKitHost host)
        {
            _host = host ?? throw new ArgumentNullException(nameof(host));

            _httpRequest = OnHttpRequest;
            _httpCancel = OnHttpCancel;
            _sseOpen = OnSseOpen;
            _sseClose = OnSseClose;
            _storageGet = OnStorageGet;
            _storageSet = OnStorageSet;
            _storageRemove = OnStorageRemove;
            _storageClear = OnStorageClear;
            _log = OnLog;

            var block = new NativeDelegates
            {
                HttpRequest = Marshal.GetFunctionPointerForDelegate(_httpRequest),
                HttpCancel = Marshal.GetFunctionPointerForDelegate(_httpCancel),
                SseOpen = host.Sse == null ? IntPtr.Zero : Marshal.GetFunctionPointerForDelegate(_sseOpen),
                SseClose = host.Sse == null ? IntPtr.Zero : Marshal.GetFunctionPointerForDelegate(_sseClose),
                StorageGet = Marshal.GetFunctionPointerForDelegate(_storageGet),
                StorageSet = Marshal.GetFunctionPointerForDelegate(_storageSet),
                StorageRemove = Marshal.GetFunctionPointerForDelegate(_storageRemove),
                StorageClear = Marshal.GetFunctionPointerForDelegate(_storageClear),
                Log = Marshal.GetFunctionPointerForDelegate(_log),
            };

            _block = Marshal.AllocHGlobal(Marshal.SizeOf<NativeDelegates>());
            Marshal.StructureToPtr(block, _block, false);
        }

        /// <summary>Pass to <see cref="WalletKitClient"/>.</summary>
        public IntPtr Handle => _block;

        // ---- HTTP --------------------------------------------------------------

        private void OnHttpRequest(IntPtr user, IntPtr client, long token, IntPtr method, IntPtr url,
                                   IntPtr headersJson, IntPtr body)
        {
            // Decode on this thread: the core owns these pointers only for the
            // duration of the call.
            string m = NativeMethods.FromUtf8(method);
            string u = NativeMethods.FromUtf8(url);
            string h = NativeMethods.FromUtf8(headersJson);
            string b = NativeMethods.FromUtf8(body);

            var cts = new CancellationTokenSource();
            _inFlight[token] = cts;

            // Fire and forget: the worker thread must not block.
            Task.Run(async () =>
            {
                try
                {
                    var response = await _host.Http.SendAsync(m, u, h, b, cts.Token).ConfigureAwait(false);
                    NativeMethods.twk_http_respond(client, token, response.Status,
                                                   NativeMethods.ToUtf8(response.HeadersJson),
                                                   NativeMethods.ToUtf8(response.Body));
                }
                catch (Exception ex)
                {
                    NativeMethods.twk_http_failed(client, token, NativeMethods.ToUtf8(ex.Message));
                }
                finally
                {
                    if (_inFlight.TryRemove(token, out var done))
                    {
                        done.Dispose();
                    }
                }
            });
        }

        private void OnHttpCancel(IntPtr user, IntPtr client, long token)
        {
            if (_inFlight.TryGetValue(token, out var cts))
            {
                try
                {
                    cts.Cancel();
                }
                catch (ObjectDisposedException)
                {
                    // Completed between the lookup and the cancel; nothing to do.
                }
            }
        }

        // ---- SSE ---------------------------------------------------------------

        private sealed class SseSink : IWalletKitSseSink
        {
            private readonly IntPtr _client;
            private readonly long _token;

            public SseSink(IntPtr client, long token)
            {
                _client = client;
                _token = token;
            }

            public void OnEvent(string json)
            {
                NativeMethods.twk_sse_event(_client, _token, NativeMethods.ToUtf8(json));
            }

            public void OnClosed(string error)
            {
                NativeMethods.twk_sse_closed(_client, _token, NativeMethods.ToUtf8(error));
            }
        }

        private void OnSseOpen(IntPtr user, IntPtr client, long token, IntPtr url, IntPtr headersJson)
        {
            string u = NativeMethods.FromUtf8(url);
            string h = NativeMethods.FromUtf8(headersJson);

            var cts = new CancellationTokenSource();
            _inFlight[token] = cts;
            var sink = new SseSink(client, token);

            Task.Run(async () =>
            {
                try
                {
                    await _host.Sse.OpenAsync(u, h, sink, cts.Token).ConfigureAwait(false);
                    sink.OnClosed(null); // clean end of stream
                }
                catch (OperationCanceledException)
                {
                    // Closed by the app; the core already dropped the stream.
                }
                catch (Exception ex)
                {
                    sink.OnClosed(ex.Message);
                }
                finally
                {
                    if (_inFlight.TryRemove(token, out var done))
                    {
                        done.Dispose();
                    }
                }
            });
        }

        private void OnSseClose(IntPtr user, IntPtr client, long token)
        {
            if (_inFlight.TryGetValue(token, out var cts))
            {
                try
                {
                    cts.Cancel();
                }
                catch (ObjectDisposedException)
                {
                }
            }
        }

        // ---- storage -----------------------------------------------------------

        private void OnStorageGet(IntPtr user, IntPtr client, long token, IntPtr key)
        {
            string k = NativeMethods.FromUtf8(key);
            Task.Run(async () =>
            {
                string value = null;
                try
                {
                    value = await _host.Storage.GetAsync(k).ConfigureAwait(false);
                }
                catch (Exception ex)
                {
                    _host.Log(3, "storage get failed: " + ex.Message);
                }
                // null => absent, which is distinct from "".
                NativeMethods.twk_storage_respond(client, token, NativeMethods.ToUtf8(value));
            });
        }

        private void OnStorageSet(IntPtr user, IntPtr client, long token, IntPtr key, IntPtr value)
        {
            string k = NativeMethods.FromUtf8(key);
            string v = NativeMethods.FromUtf8(value);
            RunStorageWrite(client, token, () => _host.Storage.SetAsync(k, v), "set");
        }

        private void OnStorageRemove(IntPtr user, IntPtr client, long token, IntPtr key)
        {
            string k = NativeMethods.FromUtf8(key);
            RunStorageWrite(client, token, () => _host.Storage.RemoveAsync(k), "remove");
        }

        private void OnStorageClear(IntPtr user, IntPtr client, long token)
        {
            RunStorageWrite(client, token, () => _host.Storage.ClearAsync(), "clear");
        }

        /// <summary>
        /// Writes must acknowledge only once durable — walletkit awaits them, so
        /// responding early would let "wallet added" resolve before it is stored.
        /// </summary>
        private void RunStorageWrite(IntPtr client, long token, Func<Task> write, string what)
        {
            Task.Run(async () =>
            {
                try
                {
                    await write().ConfigureAwait(false);
                }
                catch (Exception ex)
                {
                    _host.Log(3, "storage " + what + " failed: " + ex.Message);
                }
                NativeMethods.twk_storage_respond(client, token, NativeMethods.ToUtf8(string.Empty));
            });
        }

        private void OnLog(IntPtr user, IntPtr client, int level, IntPtr message)
        {
            _host.Log(level, NativeMethods.FromUtf8(message));
        }

        /// <summary>Frees the native block. Dispose the client first.</summary>
        public void Dispose()
        {
            if (Interlocked.Exchange(ref _disposed, 1) != 0)
            {
                return;
            }

            foreach (var entry in _inFlight)
            {
                try
                {
                    entry.Value.Cancel();
                    entry.Value.Dispose();
                }
                catch (ObjectDisposedException)
                {
                }
            }
            _inFlight.Clear();

            Marshal.FreeHGlobal(_block);
            // The delegate fields stay rooted until this object is collected, which
            // is correct: a late callback would otherwise hit freed thunks.
        }
    }
}
