//
// Copyright (c) Fela Ameghino 2026
//
// Distributed under the MIT License. (See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT)
//
// What the embedding app must provide: HTTP, SSE and secure storage.
//
// These are interfaces rather than concrete classes because the binding must not
// depend on any particular host. Unigram backs HTTP with TDLib
// sendTonCenterApiRequest and storage with PasswordVault; the desktop harness
// uses HttpClient and a dictionary. The core never does I/O itself.
//
using System;
using System.Threading;
using System.Threading.Tasks;

namespace Ton.WalletKit
{
    /// <summary>An HTTP response as the core expects it.</summary>
    public struct WalletKitHttpResponse
    {
        /// <summary>HTTP status code.</summary>
        public int Status;

        /// <summary>Response headers as a flat JSON object, or null.</summary>
        public string HeadersJson;

        /// <summary>Response body.</summary>
        public string Body;
    }

    /// <summary>
    /// Outbound HTTP. Only JSON GET/POST is ever requested — that is all
    /// walletkit's API clients use.
    /// </summary>
    public interface IWalletKitHttp
    {
        /// <summary>
        /// Performs the request. Throwing (or cancelling) is reported to the core
        /// as a transport failure, which walletkit surfaces as a network error.
        /// </summary>
        /// <param name="method">"GET" or "POST".</param>
        /// <param name="url">Absolute URL built by walletkit. A TDLib-backed host
        /// splits this into endpoint path + query/payload.</param>
        /// <param name="headersJson">Request headers as a flat JSON object.</param>
        /// <param name="body">Request body for POST, otherwise null.</param>
        /// <param name="cancellationToken">Cancelled when the core abandons the
        /// request (walletkit aborting it, or the client shutting down).</param>
        Task<WalletKitHttpResponse> SendAsync(string method, string url, string headersJson, string body,
                                              CancellationToken cancellationToken);
    }

    /// <summary>Receives server-sent events from a TON Connect relay stream.</summary>
    public interface IWalletKitSseSink
    {
        /// <summary>One dispatched SSE event: {"data":…,"event":…,"id":…}.</summary>
        void OnEvent(string json);

        /// <summary>Terminal. <paramref name="error"/> is null for a clean close.</summary>
        void OnClosed(string error);
    }

    /// <summary>The TON Connect relay transport.</summary>
    public interface IWalletKitSse
    {
        /// <summary>
        /// Opens a stream and pushes events to <paramref name="sink"/> until it
        /// closes or <paramref name="cancellationToken"/> fires. The host owns the
        /// text/event-stream framing.
        /// </summary>
        Task OpenAsync(string url, string headersJson, IWalletKitSseSink sink,
                       CancellationToken cancellationToken);
    }

    /// <summary>
    /// Secure storage. Holds TON Connect sessions — which contain session private
    /// keys — so it must be confidential (PasswordVault / Keychain / Keystore),
    /// not a plain file.
    ///
    /// Note walletkit does NOT persist wallets or signers here; the app stores the
    /// mnemonic itself and rebuilds the wallet on startup (see docs/API.md).
    /// </summary>
    public interface IWalletKitStorage
    {
        /// <summary>Returns the value, or null when absent.</summary>
        Task<string> GetAsync(string key);

        /// <summary>Must not complete before the value is durably stored.</summary>
        Task SetAsync(string key, string value);

        /// <summary>Removes a key.</summary>
        Task RemoveAsync(string key);

        /// <summary>Removes everything this client owns.</summary>
        Task ClearAsync();
    }

    /// <summary>Everything the core needs from its host.</summary>
    public interface IWalletKitHost
    {
        /// <summary>Outbound HTTP.</summary>
        IWalletKitHttp Http { get; }

        /// <summary>TON Connect relay streams; may be null to disable TON Connect.</summary>
        IWalletKitSse Sse { get; }

        /// <summary>Secure storage.</summary>
        IWalletKitStorage Storage { get; }

        /// <summary>Diagnostics. level: 0=debug 1=info 2=warn 3=error.</summary>
        void Log(int level, string message);
    }
}
