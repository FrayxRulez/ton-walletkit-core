//
// The typed entry point (kit-ios TONWalletKit): the half that is not mechanical.
//
// Everything that is just "call an ABI method and read the result" is generated
// into TonWalletKit.g.cs from api/facade.json. What stays here is the transport,
// the event plumbing, and the few methods whose bodies are genuinely bespoke —
// each marked `custom` in the schema so the two halves cannot drift apart.
//
using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using Ton.WalletKit.Api.Model;

namespace Ton.WalletKit
{
    /// <summary>Receives bridge events (kit-ios TONBridgeEventsHandler).</summary>
    public interface ITonBridgeEventsHandler
    {
        /// <summary>Handles one event. Throwing here does not stop the pump.</summary>
        void Handle(TonWalletKitEvent walletKitEvent);
    }

    /// <summary>The wallet kit: keys, wallets, and TON Connect.</summary>
    public sealed partial class TonWalletKit : IDisposable
    {
        private readonly WalletKitClient _client;
        private readonly WalletKitDelegates _delegates;
        private readonly TonWalletKitConfiguration _configuration;
        private readonly List<ITonBridgeEventsHandler> _handlers = new List<ITonBridgeEventsHandler>();

        private int _initialized;

        /// <summary>A dapp asked for something. Show it, then approve or reject.</summary>
        public event EventHandler<TonWalletKitEvent> EventReceived;

        /// <summary>True once <see cref="InitializeAsync"/> has completed.</summary>
        public bool IsInitialized
        {
            get { return Volatile.Read(ref _initialized) != 0; }
        }

        /// <summary>How this kit was configured.</summary>
        public TonWalletKitConfiguration Configuration
        {
            get { return _configuration; }
        }

        /// <summary>
        /// Creates a kit. <paramref name="host"/> provides HTTP, storage and SSE;
        /// nothing happens until <see cref="InitializeAsync"/> is awaited.
        /// </summary>
        public TonWalletKit(IWalletKitHost host, TonWalletKitConfiguration configuration)
        {
            if (configuration == null)
            {
                throw new ArgumentNullException(nameof(configuration));
            }

            _configuration = configuration;
            _delegates = new WalletKitDelegates(host);
            _client = new WalletKitClient(_delegates.Handle, IntPtr.Zero);
            _client.EventReceived += OnEventReceived;
        }

        // ---- transport ---------------------------------------------------------

        /// <summary>Invokes a core method and returns the raw envelope.</summary>
        internal Task<string> CallAsync(string method, string args)
        {
            return _client.SendAsync(method, args);
        }

        /// <summary>Invokes a core method and reads its result.</summary>
        internal async Task<T> InvokeAsync<T>(string method, string args)
        {
            string envelope = await _client.SendAsync(method, args).ConfigureAwait(false);
            return TonJson.Result<T>(envelope);
        }

        // ---- the ids the ABI addresses objects by ------------------------------

        /// <summary>The core's id for an adapter or wallet this kit created.</summary>
        internal string Handle(ITonWalletAdapter adapter, string parameterName)
        {
            var owned = adapter as TonWalletAdapter;
            if (owned == null)
            {
                throw new ArgumentException("The adapter must come from this kit.", parameterName);
            }
            return owned.Handle;
        }

        /// <summary>The core's id for a signer this kit created.</summary>
        internal string SignerId(ITonWalletSigner signer, string parameterName)
        {
            var owned = signer as TonWalletSigner;
            if (owned == null)
            {
                throw new ArgumentException("The signer must come from this kit.", parameterName);
            }
            return owned.Handle;
        }

        /// <summary>The caller's network, or the first one configured.</summary>
        internal string ChainId(string chainId)
        {
            if (chainId != null)
            {
                return chainId;
            }

            var networks = _configuration.NetworkConfigurations;
            return networks != null && networks.Count > 0 ? networks[0].ChainId : null;
        }

        // ---- custom methods (declared in api/facade.json) ----------------------

        /// <summary>
        /// Builds the kit inside the core. Safe to call more than once; the second
        /// call rebuilds it, exactly as calling initWalletKit twice would.
        /// </summary>
        public async Task InitializeAsync()
        {
            await CallAsync("initWalletKit", TonJson.ArgsRaw(_configuration.ToJson())).ConfigureAwait(false);
            Volatile.Write(ref _initialized, 1);
        }

        /// <summary>
        /// Generates a mnemonic and derives a V5R1 wallet from it in one step
        /// (kit-ios createWallet). The wallet is not registered yet — pass the
        /// adapter to <see cref="AddAsync"/>.
        /// </summary>
        public async Task<TonWalletCreationResult> CreateWalletAsync(TonV5R1WalletParameters parameters)
        {
            var mnemonic = await GenerateMnemonicAsync().ConfigureAwait(false);
            var signer = await SignerAsync(mnemonic).ConfigureAwait(false);
            var adapter = await WalletV5R1AdapterAsync(signer, parameters).ConfigureAwait(false);
            return new TonWalletCreationResult(mnemonic, adapter);
        }

        /// <summary>Signs and broadcasts a transaction from a wallet.</summary>
        public Task<TONSendTransactionResponse> SendAsync(TONTransactionRequest transaction, ITonWallet wallet)
        {
            if (wallet == null)
            {
                throw new ArgumentNullException(nameof(wallet));
            }

            return wallet.SendAsync(transaction);
        }

        /// <summary>Balance of any address, in nanotons.</summary>
        public async Task<TonAmount> AddressBalanceAsync(string address, string chainId = null)
        {
            // {address, balance} is a shape of the core's own, not a walletkit model.
            var reply = await InvokeAsync<TonAddressBalance>("getAddressBalance",
                                                             TonJson.Args(address, ChainId(chainId)))
                                  .ConfigureAwait(false);
            return TonAmount.FromNanotons(reply == null ? null : reply.Balance);
        }

        /// <summary>
        /// Handles a tc:// link. The dapp's request arrives as an event, not as
        /// the result of this call.
        /// </summary>
        public Task ConnectAsync(string url)
        {
            if (url != null && url.IndexOf("://", StringComparison.Ordinal) < 0)
            {
                url = "tc://" + url.TrimStart('/');
            }
            return CallAsync("handleTonConnectUrl", TonJson.Args(url));
        }

        // ---- events ------------------------------------------------------------

        /// <summary>Adds an events handler (kit-ios add(eventsHandler:)).</summary>
        public void AddEventsHandler(ITonBridgeEventsHandler handler)
        {
            if (handler == null)
            {
                throw new ArgumentNullException(nameof(handler));
            }

            lock (_handlers)
            {
                if (!_handlers.Contains(handler))
                {
                    _handlers.Add(handler);
                }
            }
        }

        /// <summary>Removes an events handler.</summary>
        public void RemoveEventsHandler(ITonBridgeEventsHandler handler)
        {
            lock (_handlers)
            {
                _handlers.Remove(handler);
            }
        }

        private void OnEventReceived(object sender, WalletKitEventArgs e)
        {
            string type;
            string payload;
            if (!TonJson.TryReadEvent(e.Json, out type, out payload))
            {
                return;
            }

            var walletKitEvent = Build(type, payload);
            if (walletKitEvent == null)
            {
                return;
            }

            var handler = EventReceived;
            if (handler != null)
            {
                handler(this, walletKitEvent);
            }

            ITonBridgeEventsHandler[] handlers;
            lock (_handlers)
            {
                handlers = _handlers.ToArray();
            }

            foreach (var each in handlers)
            {
                try
                {
                    each.Handle(walletKitEvent);
                }
                catch
                {
                    // One handler must not stop the others, or the pump.
                }
            }
        }

        private TonWalletKitEvent Build(string type, string payload)
        {
            switch (type)
            {
                case "connectRequest":
                    return new TonWalletKitEvent(TonWalletKitEventType.ConnectRequest)
                    {
                        ConnectRequest = new TonWalletConnectionRequest(
                            this, TonJson.Deserialize<TONConnectionRequestEvent>(payload)),
                    };
                case "transactionRequest":
                    return new TonWalletKitEvent(TonWalletKitEventType.TransactionRequest)
                    {
                        TransactionRequest = new TonWalletSendTransactionRequest(
                            this, TonJson.Deserialize<TONSendTransactionRequestEvent>(payload)),
                    };
                case "signMessageRequest":
                    return new TonWalletKitEvent(TonWalletKitEventType.SignMessageRequest)
                    {
                        SignMessageRequest = new TonWalletSignMessageRequest(
                            this, TonJson.Deserialize<TONSignMessageRequestEvent>(payload)),
                    };
                case "signDataRequest":
                    return new TonWalletKitEvent(TonWalletKitEventType.SignDataRequest)
                    {
                        SignDataRequest = new TonWalletSignDataRequest(
                            this, TonJson.Deserialize<TONSignDataRequestEvent>(payload)),
                    };
                case "disconnect":
                    return new TonWalletKitEvent(TonWalletKitEventType.Disconnect)
                    {
                        Disconnect = TonJson.Deserialize<TONDisconnectionEvent>(payload),
                    };
                default:
                    // Diagnostics and probes emit events too; they are not requests.
                    return null;
            }
        }

        /// <summary>
        /// Turns an approveConnectRequest reply into the follow-up request the
        /// dapp embedded, or null when it embedded none.
        /// </summary>
        internal TonWalletKitEvent EmbeddedEvent(string envelope)
        {
            string result;
            if (!TonJson.TryResultJson(envelope, out result))
            {
                return null; // the dapp embedded nothing
            }

            // The generated union model carries only the discriminator, so the
            // payload is read again as the variant it names.
            var kind = TonJson.Deserialize<TONEmbeddedRequestEvent>(result);
            if (kind == null)
            {
                return null;
            }

            switch (kind.Type)
            {
                case TONEmbeddedRequestEvent.TypeEnum.SendTransaction:
                    return new TonWalletKitEvent(TonWalletKitEventType.TransactionRequest)
                    {
                        TransactionRequest = new TonWalletSendTransactionRequest(
                            this, TonJson.Deserialize<TONEmbeddedSendTransactionRequestEvent>(result)),
                    };
                case TONEmbeddedRequestEvent.TypeEnum.SignMessage:
                    return new TonWalletKitEvent(TonWalletKitEventType.SignMessageRequest)
                    {
                        SignMessageRequest = new TonWalletSignMessageRequest(
                            this, TonJson.Deserialize<TONEmbeddedSignMessageRequestEvent>(result)),
                    };
                case TONEmbeddedRequestEvent.TypeEnum.SignData:
                    return new TonWalletKitEvent(TonWalletKitEventType.SignDataRequest)
                    {
                        SignDataRequest = new TonWalletSignDataRequest(
                            this, TonJson.Deserialize<TONEmbeddedSignDataRequestEvent>(result)),
                    };
                default:
                    return null;
            }
        }

        /// <summary>Tears down the kit. Disposes the client before the delegates.</summary>
        public void Dispose()
        {
            _client.EventReceived -= OnEventReceived;
            _client.Dispose();    // joins the receive thread and destroys the core client
            _delegates.Dispose(); // only safe once nothing can call back
        }
    }

    /// <summary>The custom half of the connect request: approving is not mechanical.</summary>
    public sealed partial class TonWalletConnectionRequest
    {
        /// <summary>
        /// Connects <paramref name="wallet"/> to the dapp. Returns a follow-up
        /// request when the dapp embedded one in the connect (a transaction to
        /// approve straight away), otherwise null.
        /// </summary>
        public async Task<TonWalletKitEvent> ApproveAsync(ITonWallet wallet,
                                                          TONConnectionApprovalResponse response = null)
        {
            if (wallet == null)
            {
                throw new ArgumentNullException(nameof(wallet));
            }

            // The wallet the user chose. walletkit rejects an approval without it.
            Event.WalletId = wallet.Id;
            Event.WalletAddress = wallet.Address;

            string envelope = await _kit.CallAsync("approveConnectRequest", TonJson.Args(Event, response))
                                        .ConfigureAwait(false);
            return _kit.EmbeddedEvent(envelope);
        }
    }
}
