//
// The typed API. Restores the object model kit-ios exposes (wallet.GetBalance())
// on top of an ABI that can only carry ids — see docs/API.md.
//
// Naming follows kit-ios; object arguments become the corresponding id, because a
// JSON ABI cannot carry objects.
//
using System;
using System.Collections.Generic;
using System.Threading.Tasks;

namespace Ton.WalletKit
{
    /// <summary>A signer living in the core; the secret never crosses the ABI.</summary>
    public sealed class TonSigner
    {
        /// <summary>Id used to address this signer.</summary>
        public string SignerId { get; }

        /// <summary>Public key, 0x-prefixed hex.</summary>
        public string PublicKey { get; }

        internal TonSigner(string signerId, string publicKey)
        {
            SignerId = signerId;
            PublicKey = publicKey;
        }
    }

    /// <summary>A wallet contract adapter (V5R1 / V4R2).</summary>
    public sealed class TonWalletAdapter
    {
        /// <summary>Id used to address this adapter.</summary>
        public string AdapterId { get; }

        /// <summary>The wallet address this adapter derives.</summary>
        public string Address { get; }

        /// <summary>Network chain id ("-3" testnet, "-239" mainnet).</summary>
        public string ChainId { get; }

        internal TonWalletAdapter(string adapterId, string address, string chainId)
        {
            AdapterId = adapterId;
            Address = address;
            ChainId = chainId;
        }
    }

    /// <summary>
    /// A wallet registered with the kit. Note this is a handle, not storage:
    /// walletkit keeps wallets in memory only, so after a restart the app must
    /// rebuild it from the persisted mnemonic (see docs/API.md).
    /// </summary>
    public sealed class TonWallet
    {
        private readonly TonWalletKit _kit;

        /// <summary>The kit's wallet id (a base64 hash, not network:address).</summary>
        public string WalletId { get; }

        /// <summary>User-friendly address.</summary>
        public string Address { get; }

        /// <summary>Public key, 0x-prefixed hex.</summary>
        public string PublicKey { get; }

        internal TonWallet(TonWalletKit kit, string walletId, string address, string publicKey)
        {
            _kit = kit;
            WalletId = walletId;
            Address = address;
            PublicKey = publicKey;
        }

        /// <summary>Current balance.</summary>
        public async Task<TonAmount> GetBalanceAsync()
        {
            string json = await _kit.CallAsync("getBalance", Json.Args(Json.Quote(WalletId))).ConfigureAwait(false);
            return TonAmount.FromNanotons(Json.GetString(json, "balance"));
        }

        /// <summary>Full account state for this wallet's address.</summary>
        public Task<TonAccountState> GetStateAsync() => _kit.GetAddressStateAsync(Address);

        /// <summary>
        /// Builds an unsigned TON transfer. <paramref name="recipientAddress"/> must
        /// be user-friendly form (UQ…/EQ…): the raw form passes an early check and
        /// is then rejected with a message that names the transaction, not the
        /// address. Returns the transaction JSON to sign or preview.
        /// </summary>
        public async Task<string> CreateTransferAsync(string recipientAddress, TonAmount amount,
                                                      string comment = null)
        {
            string request = "{\"recipientAddress\":" + Json.Quote(recipientAddress) +
                             ",\"transferAmount\":" + Json.Quote(amount.ToRawString()) +
                             (comment == null ? "" : ",\"comment\":" + Json.Quote(comment)) + "}";
            string json = await _kit.CallAsync("createTransferTonTransaction",
                                               Json.Args(Json.Quote(WalletId), request)).ConfigureAwait(false);
            return Json.Result(json);
        }

        /// <summary>Fee/effect preview for a built transaction.</summary>
        public async Task<string> GetTransactionPreviewAsync(string transactionJson)
        {
            string json = await _kit.CallAsync("getTransactionPreview",
                                               Json.Args(Json.Quote(WalletId), transactionJson)).ConfigureAwait(false);
            return Json.Result(json);
        }

        /// <summary>
        /// Signs and returns the BOC. <paramref name="fakeSignature"/> produces a
        /// correctly shaped but unusable signature — right for fee estimation, and
        /// what tests use so nothing can be broadcast by accident.
        /// </summary>
        public async Task<string> GetSignedTransactionAsync(string transactionJson, bool fakeSignature = false)
        {
            string options = "{\"fakeSignature\":" + (fakeSignature ? "true" : "false") + "}";
            string json = await _kit.CallAsync("getSignedSendTransaction",
                                               Json.Args(Json.Quote(WalletId), transactionJson, options))
                                    .ConfigureAwait(false);
            return Json.GetString(json, "boc");
        }

        /// <summary>Signs and broadcasts. This spends real funds.</summary>
        public async Task<string> SendTransactionAsync(string transactionJson)
        {
            string json = await _kit.CallAsync("sendTransaction",
                                               Json.Args(Json.Quote(WalletId), transactionJson)).ConfigureAwait(false);
            return Json.Result(json);
        }
    }

    /// <summary>A TON Connect request awaiting the user's decision.</summary>
    public sealed class TonConnectRequest
    {
        /// <summary>connectRequest | transactionRequest | signDataRequest | signMessageRequest | disconnect.</summary>
        public string Type { get; }

        /// <summary>The raw event payload, passed back verbatim when approving.</summary>
        public string Payload { get; }

        internal TonConnectRequest(string type, string payload)
        {
            Type = type;
            Payload = payload;
        }
    }

    /// <summary>The typed entry point (kit-ios TonWalletKit analog).</summary>
    public sealed class TonWalletKit : IDisposable
    {
        private readonly WalletKitClient _client;
        private readonly WalletKitDelegates _delegates;

        /// <summary>A dapp is asking for something; show it and approve or reject.</summary>
        public event EventHandler<TonConnectRequest> RequestReceived;

        /// <summary>Creates a kit backed by <paramref name="host"/>.</summary>
        public TonWalletKit(IWalletKitHost host)
        {
            _delegates = new WalletKitDelegates(host);
            _client = new WalletKitClient(_delegates.Handle, IntPtr.Zero);
            _client.EventReceived += OnEventReceived;
        }

        internal Task<string> CallAsync(string method, string args) => _client.SendAsync(method, args);

        private void OnEventReceived(object sender, WalletKitEventArgs e)
        {
            string type = Json.GetString(e.Json, "type");
            if (type == null)
            {
                return;
            }

            // The payload is handed back verbatim to approve/reject.
            const string key = "\"payload\":";
            int at = e.Json.IndexOf(key, StringComparison.Ordinal);
            string payload = at < 0 ? null : e.Json.Substring(at + key.Length).TrimEnd('}');

            RequestReceived?.Invoke(this, new TonConnectRequest(type, payload));
        }

        /// <summary>
        /// Builds the kit. <paramref name="bridgeJson"/> is required for TON Connect:
        /// approvals travel back to the dapp through the relay, and without it
        /// approving fails with "Bridge not initialized for sending response".
        /// </summary>
        public Task InitializeAsync(string chainId = "-3", string endpoint = null, string bridgeJson = null)
        {
            string network = "{\"chainId\":" + Json.Quote(chainId) +
                             (endpoint == null ? "" : ",\"endpoint\":" + Json.Quote(endpoint)) + "}";
            string config = "{\"networks\":[" + network + "]" +
                            (bridgeJson == null ? "" : ",\"bridge\":" + bridgeJson) + "}";
            return CallAsync("initWalletKit", "[" + config + "]");
        }

        /// <summary>Generates a 24-word TON mnemonic.</summary>
        public async Task<IReadOnlyList<string>> CreateMnemonicAsync()
        {
            string json = await CallAsync("createMnemonic", "[]").ConfigureAwait(false);
            var words = new List<string>(24);
            foreach (string part in Json.Result(json).Trim('[', ']').Split(','))
            {
                words.Add(part.Trim().Trim('"'));
            }
            return words;
        }

        /// <summary>
        /// Creates a signer. The app is responsible for storing the mnemonic
        /// securely — walletkit never persists signers.
        /// </summary>
        public async Task<TonSigner> CreateSignerAsync(IEnumerable<string> mnemonic)
        {
            var quoted = new List<string>();
            foreach (string word in mnemonic)
            {
                quoted.Add(Json.Quote(word));
            }
            string args = "[[" + string.Join(",", quoted) + "],\"ton\"]";
            string json = await CallAsync("createSignerFromMnemonic", args).ConfigureAwait(false);
            return new TonSigner(Json.GetString(json, "signerId"), Json.GetString(json, "publicKey"));
        }

        /// <summary>Creates a V5R1 adapter (the current default wallet contract).</summary>
        public async Task<TonWalletAdapter> CreateV5R1AdapterAsync(TonSigner signer, string chainId = "-3")
        {
            string args = Json.Args(Json.Quote(signer.SignerId), "{\"chainId\":" + Json.Quote(chainId) + "}");
            string json = await CallAsync("createV5R1WalletAdapter", args).ConfigureAwait(false);
            return new TonWalletAdapter(Json.GetString(json, "adapterId"), Json.GetString(json, "address"), chainId);
        }

        /// <summary>Registers the adapter, yielding a usable wallet.</summary>
        public async Task<TonWallet> AddWalletAsync(TonWalletAdapter adapter)
        {
            string json = await CallAsync("addWallet", Json.Args(Json.Quote(adapter.AdapterId))).ConfigureAwait(false);
            return new TonWallet(this, Json.GetString(json, "walletId"), Json.GetString(json, "address"),
                                 Json.GetString(json, "publicKey"));
        }

        /// <summary>Convenience: mnemonic to wallet in one step.</summary>
        public async Task<TonWallet> RestoreWalletAsync(IEnumerable<string> mnemonic, string chainId = "-3")
        {
            var signer = await CreateSignerAsync(mnemonic).ConfigureAwait(false);
            var adapter = await CreateV5R1AdapterAsync(signer, chainId).ConfigureAwait(false);
            return await AddWalletAsync(adapter).ConfigureAwait(false);
        }

        // ---- watch-only lookups (no wallet or signer required) -----------------

        /// <summary>
        /// Account state for any address: balance, status, last transaction.
        /// Useful for inspecting an address the user does not control.
        /// </summary>
        public async Task<TonAccountState> GetAddressStateAsync(string address, string chainId = "-3")
        {
            string json = await CallAsync("getAddressState",
                                          Json.Args(Json.Quote(address), Json.Quote(chainId))).ConfigureAwait(false);
            return new TonAccountState
            {
                Address = Json.GetString(json, "address") ?? address,
                Status = Json.GetString(json, "status"),
                // rawBalance is the exact integer; "balance" is a formatted decimal.
                Balance = TonAmount.FromNanotons(Json.GetString(json, "rawBalance")),
                LastTransactionLt = Json.GetString(json, "lt"),
                LastTransactionHash = Json.GetString(json, "hash"),
                RawJson = Json.Result(json),
            };
        }

        /// <summary>
        /// Transaction history for any address.
        /// </summary>
        /// <remarks>
        /// Currently throws for most addresses: walletkit's own response parser
        /// (toTransactionsResponse) rejects well-formed toncenter replies with
        /// "Invalid hash: data is required". The raw endpoint returns valid data,
        /// so this is an upstream limitation, not a transport problem. Kept here so
        /// it starts working when walletkit is updated.
        /// </remarks>
        public async Task<string> GetAddressTransactionsAsync(string address, string chainId = "-3", int limit = 10)
        {
            string json = await CallAsync("getAddressTransactions",
                                          Json.Args(Json.Quote(address), Json.Quote(chainId),
                                                    limit.ToString(System.Globalization.CultureInfo.InvariantCulture)))
                                .ConfigureAwait(false);
            return Json.Result(json);
        }

        // ---- TON Connect -------------------------------------------------------

        /// <summary>Handles a tc:// link; the request arrives via RequestReceived.</summary>
        public Task HandleTonConnectUrlAsync(string url) =>
            CallAsync("handleTonConnectUrl", Json.Args(Json.Quote(url)));

        /// <summary>
        /// Approves a request with the given wallet. The walletId must be attached
        /// to the event — that is the user choosing an account, and without it
        /// walletkit rejects the approval.
        /// </summary>
        public Task ApproveAsync(TonConnectRequest request, TonWallet wallet)
        {
            string payload = request.Payload;
            if (wallet != null && payload != null && payload.StartsWith("{", StringComparison.Ordinal))
            {
                payload = "{\"walletId\":" + Json.Quote(wallet.WalletId) + "," + payload.Substring(1);
            }
            return CallAsync(ApprovalMethod(request.Type, approve: true), "[" + payload + "]");
        }

        /// <summary>Rejects a request.</summary>
        public Task RejectAsync(TonConnectRequest request, string reason = null)
        {
            string args = reason == null ? "[" + request.Payload + "]"
                                         : "[" + request.Payload + "," + Json.Quote(reason) + "]";
            return CallAsync(ApprovalMethod(request.Type, approve: false), args);
        }

        private static string ApprovalMethod(string type, bool approve)
        {
            string verb = approve ? "approve" : "reject";
            switch (type)
            {
                case "connectRequest": return verb + "ConnectRequest";
                case "transactionRequest": return verb + "TransactionRequest";
                case "signDataRequest": return verb + "SignDataRequest";
                case "signMessageRequest": return verb + "SignMessageRequest";
                default: throw new ArgumentException("Not an approvable request: " + type, nameof(type));
            }
        }

        /// <summary>Tears down the kit. Disposes the client before the delegates.</summary>
        public void Dispose()
        {
            _client.EventReceived -= OnEventReceived;
            _client.Dispose();   // joins the receive thread and destroys the core client
            _delegates.Dispose(); // only safe once nothing can call back
        }
    }
}
