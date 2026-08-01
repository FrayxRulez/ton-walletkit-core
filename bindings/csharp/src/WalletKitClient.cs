//
// Copyright (c) Fela Ameghino 2026
//
// Distributed under the MIT License. (See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT)
//
// The transport half of the C# binding: one core client, a dedicated receive
// thread, and request/response correlation. Mirrors the Telegram.Td client shape.
//
// Everything above this (the typed facade) is built on SendAsync + EventReceived.
//
using System;
using System.Collections.Concurrent;
using System.Threading;
using System.Threading.Tasks;
using Ton.WalletKit.Native;

namespace Ton.WalletKit
{
    /// <summary>An unsolicited update from the core (request_id = 0).</summary>
    public sealed class WalletKitEventArgs : EventArgs
    {
        /// <summary>The raw <c>{"event":{…}}</c> payload, as JSON.</summary>
        public string Json { get; }

        /// <summary>Wraps the raw event JSON.</summary>
        public WalletKitEventArgs(string json)
        {
            Json = json;
        }
    }

    /// <summary>Raised when the core reports an error for a request.</summary>
    public sealed class WalletKitException : Exception
    {
        /// <summary>The raw <c>{"error":{…}}</c> envelope.</summary>
        public string Json { get; }

        /// <summary>Creates an exception from the core's error envelope.</summary>
        public WalletKitException(string message, string json) : base(message)
        {
            Json = json;
        }
    }

    /// <summary>
    /// Owns a native client and pumps its message queue.
    ///
    /// Threading: <see cref="SendAsync"/> is safe from any thread; a single
    /// dedicated thread calls twk_receive, as the ABI requires (receive must not
    /// be called concurrently for one client).
    /// </summary>
    public sealed class WalletKitClient : IDisposable
    {
        private readonly IntPtr _client;
        private readonly Thread _receiveThread;
        private readonly ConcurrentDictionary<ulong, TaskCompletionSource<string>> _pending =
            new ConcurrentDictionary<ulong, TaskCompletionSource<string>>();

        private long _nextRequestId;
        private volatile bool _running = true;
        private int _disposed;

        /// <summary>Unsolicited updates: TON Connect requests, disconnects, …</summary>
        public event EventHandler<WalletKitEventArgs> EventReceived;

        /// <summary>
        /// Creates a client. <paramref name="delegates"/> is a pointer to a native
        /// twk_delegates block (see WalletKitDelegates); pass IntPtr.Zero to run
        /// without host I/O, which is only useful for diagnostics.
        /// </summary>
        public WalletKitClient(IntPtr delegates, IntPtr user)
        {
            _client = NativeMethods.twk_client_create(delegates, user);
            if (_client == IntPtr.Zero)
            {
                throw new InvalidOperationException("twk_client_create failed");
            }

            _receiveThread = new Thread(ReceiveLoop)
            {
                Name = "twk-receive",
                IsBackground = true,
            };
            _receiveThread.Start();
        }

        /// <summary>Native handle, for the delegate layer's completion calls.</summary>
        internal IntPtr Handle => _client;

        /// <summary>
        /// Invokes a method on the core. <paramref name="paramsJson"/> is a
        /// positional argument array (see docs/API.md). Faults with
        /// <see cref="WalletKitException"/> when the core returns an error.
        /// </summary>
        public Task<string> SendAsync(string method, string paramsJson)
        {
            if (method == null)
            {
                throw new ArgumentNullException(nameof(method));
            }

            // 0 is reserved for unsolicited updates, so ids start at 1.
            ulong requestId = unchecked((ulong)Interlocked.Increment(ref _nextRequestId));

            // Continuations must not run on the receive thread — a caller awaiting
            // this would otherwise stall the pump for every other request.
            var completion = new TaskCompletionSource<string>(TaskCreationOptions.RunContinuationsAsynchronously);
            _pending[requestId] = completion;

            try
            {
                NativeMethods.twk_send(_client, requestId, NativeMethods.ToUtf8(method),
                                       NativeMethods.ToUtf8(paramsJson ?? "[]"));
            }
            catch
            {
                _pending.TryRemove(requestId, out _);
                throw;
            }

            return completion.Task;
        }

        private void ReceiveLoop()
        {
            while (_running)
            {
                ulong requestId;
                IntPtr message = NativeMethods.twk_receive(_client, 0.25, out requestId);
                if (message == IntPtr.Zero)
                {
                    continue; // timeout: loop so _running is re-checked
                }

                // The buffer is only valid until the next receive on this thread.
                string json = NativeMethods.FromUtf8(message);

                if (requestId == 0)
                {
                    RaiseEvent(json);
                    continue;
                }

                if (_pending.TryRemove(requestId, out var completion))
                {
                    Complete(completion, json);
                }
                // An id we no longer track (a cancelled or duplicate response) is
                // dropped rather than treated as an error.
            }
        }

        private static void Complete(TaskCompletionSource<string> completion, string json)
        {
            // Envelope is {"result":…} or {"error":{"message":…}}; the facade parses
            // the result, but errors become exceptions here so callers can just await.
            if (json != null && json.StartsWith("{\"error\"", StringComparison.Ordinal))
            {
                completion.TrySetException(new WalletKitException(ExtractMessage(json), json));
            }
            else
            {
                completion.TrySetResult(json);
            }
        }

        /// <summary>Pulls "message" out of an error envelope without a JSON parser.</summary>
        private static string ExtractMessage(string json)
        {
            const string key = "\"message\":\"";
            int start = json.IndexOf(key, StringComparison.Ordinal);
            if (start < 0)
            {
                return "walletkit error";
            }

            start += key.Length;
            int end = json.IndexOf('"', start);
            return end < 0 ? "walletkit error" : json.Substring(start, end - start);
        }

        private void RaiseEvent(string json)
        {
            var handler = EventReceived;
            if (handler == null)
            {
                return;
            }

            try
            {
                handler(this, new WalletKitEventArgs(json));
            }
            catch
            {
                // A subscriber must not be able to kill the pump.
            }
        }

        /// <summary>Stops the receive loop, destroys the client, and fails any
        /// outstanding requests.</summary>
        public void Dispose()
        {
            if (Interlocked.Exchange(ref _disposed, 1) != 0)
            {
                return;
            }

            _running = false;
            // Join before destroying: the receive thread is inside twk_receive with
            // the handle, and the ABI forbids using it after destroy.
            if (_receiveThread != null && _receiveThread.IsAlive)
            {
                _receiveThread.Join(TimeSpan.FromSeconds(5));
            }

            NativeMethods.twk_client_destroy(_client);

            // Fail anything still outstanding rather than leaving callers hanging.
            foreach (var entry in _pending)
            {
                entry.Value.TrySetException(new ObjectDisposedException(nameof(WalletKitClient)));
            }
            _pending.Clear();
        }
    }
}
