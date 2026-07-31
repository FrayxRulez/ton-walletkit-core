//
// What a dapp asks for, and how the app answers (kit-ios Bridge + Requests).
//
// The core delivers these as unsolicited updates; each one carries the event the
// approval must be sent back with, so the request object holds it and hands it
// over verbatim — with the wallet the user picked attached, which walletkit
// requires (approving without a walletId fails with WALLET_REQUIRED).
//
using System;
using System.Threading.Tasks;
using Ton.WalletKit.Api.Model;

namespace Ton.WalletKit
{
    /// <summary>Which kind of request arrived (kit-ios TONWalletKitEvent cases).</summary>
    public enum TonWalletKitEventType
    {
        /// <summary>A dapp wants to connect.</summary>
        ConnectRequest,

        /// <summary>A dapp wants a transaction sent.</summary>
        TransactionRequest,

        /// <summary>A dapp wants a message signed.</summary>
        SignMessageRequest,

        /// <summary>A dapp wants data signed.</summary>
        SignDataRequest,

        /// <summary>A dapp (or the wallet) ended a session.</summary>
        Disconnect,
    }

    /// <summary>
    /// One event from the bridge (kit-ios TONWalletKitEvent). Exactly one of the
    /// request properties is set, matching <see cref="Type"/>.
    /// </summary>
    public sealed class TonWalletKitEvent : EventArgs
    {
        /// <summary>Which kind of event this is.</summary>
        public TonWalletKitEventType Type { get; }

        /// <summary>Set when <see cref="Type"/> is ConnectRequest.</summary>
        public TonWalletConnectionRequest ConnectRequest { get; internal set; }

        /// <summary>Set when <see cref="Type"/> is TransactionRequest.</summary>
        public TonWalletSendTransactionRequest TransactionRequest { get; internal set; }

        /// <summary>Set when <see cref="Type"/> is SignMessageRequest.</summary>
        public TonWalletSignMessageRequest SignMessageRequest { get; internal set; }

        /// <summary>Set when <see cref="Type"/> is SignDataRequest.</summary>
        public TonWalletSignDataRequest SignDataRequest { get; internal set; }

        /// <summary>Set when <see cref="Type"/> is Disconnect.</summary>
        public TONDisconnectionEvent Disconnect { get; internal set; }

        internal TonWalletKitEvent(TonWalletKitEventType type)
        {
            Type = type;
        }
    }

    /// <summary>A dapp asking to connect (kit-ios TONWalletConnectionRequest).</summary>
    public sealed class TonWalletConnectionRequest
    {
        private readonly TonWalletKit _kit;

        /// <summary>The request, including what the dapp is and what it wants.</summary>
        public TONConnectionRequestEvent Event { get; }

        internal TonWalletConnectionRequest(TonWalletKit kit, TONConnectionRequestEvent request)
        {
            _kit = kit;
            Event = request;
        }

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

        /// <summary>Refuses the connection.</summary>
        public Task RejectAsync(string reason = null)
        {
            return _kit.CallAsync("rejectConnectRequest", TonJson.Args(Event, reason));
        }
    }

    /// <summary>A dapp asking for a transaction (kit-ios TONWalletSendTransactionRequest).</summary>
    public sealed class TonWalletSendTransactionRequest
    {
        private readonly TonWalletKit _kit;

        /// <summary>The request, with the emulated preview for the UI.</summary>
        public TONSendTransactionRequestEvent Event { get; }

        /// <summary>Set when the request came embedded in a connect approval.</summary>
        public TONEmbeddedSendTransactionRequestEvent EmbeddedEvent { get; }

        internal TonWalletSendTransactionRequest(TonWalletKit kit, TONSendTransactionRequestEvent request)
        {
            _kit = kit;
            Event = request;
        }

        internal TonWalletSendTransactionRequest(TonWalletKit kit, TONEmbeddedSendTransactionRequestEvent embedded)
        {
            _kit = kit;
            EmbeddedEvent = embedded;
        }

        /// <summary>Signs and sends the transaction.</summary>
        public Task<TONSendTransactionApprovalResponse> ApproveAsync(
            TONSendTransactionApprovalResponse response = null)
        {
            return EmbeddedEvent != null
                ? _kit.InvokeAsync<TONSendTransactionApprovalResponse>(
                      "approveTransactionRequest", TonJson.Args(EmbeddedEvent, response))
                : _kit.InvokeAsync<TONSendTransactionApprovalResponse>(
                      "approveTransactionRequest", TonJson.Args(Event, response));
        }

        /// <summary>Refuses the transaction.</summary>
        public Task RejectAsync(string reason = null)
        {
            return EmbeddedEvent != null
                ? _kit.CallAsync("rejectTransactionRequest", TonJson.Args(EmbeddedEvent, reason))
                : _kit.CallAsync("rejectTransactionRequest", TonJson.Args(Event, reason));
        }
    }

    /// <summary>A dapp asking for a signed message (kit-ios TONWalletSignMessageRequest).</summary>
    public sealed class TonWalletSignMessageRequest
    {
        private readonly TonWalletKit _kit;

        /// <summary>The request.</summary>
        public TONSignMessageRequestEvent Event { get; }

        /// <summary>Set when the request came embedded in a connect approval.</summary>
        public TONEmbeddedSignMessageRequestEvent EmbeddedEvent { get; }

        internal TonWalletSignMessageRequest(TonWalletKit kit, TONSignMessageRequestEvent request)
        {
            _kit = kit;
            Event = request;
        }

        internal TonWalletSignMessageRequest(TonWalletKit kit, TONEmbeddedSignMessageRequestEvent embedded)
        {
            _kit = kit;
            EmbeddedEvent = embedded;
        }

        /// <summary>Signs the message.</summary>
        public Task<TONSignMessageApprovalResponse> ApproveAsync(TONSignMessageApprovalResponse response = null)
        {
            return EmbeddedEvent != null
                ? _kit.InvokeAsync<TONSignMessageApprovalResponse>(
                      "approveSignMessageRequest", TonJson.Args(EmbeddedEvent, response))
                : _kit.InvokeAsync<TONSignMessageApprovalResponse>(
                      "approveSignMessageRequest", TonJson.Args(Event, response));
        }

        /// <summary>Refuses to sign.</summary>
        public Task RejectAsync(string reason = null)
        {
            return EmbeddedEvent != null
                ? _kit.CallAsync("rejectSignMessageRequest", TonJson.Args(EmbeddedEvent, reason))
                : _kit.CallAsync("rejectSignMessageRequest", TonJson.Args(Event, reason));
        }
    }

    /// <summary>A dapp asking for signed data (kit-ios TONWalletSignDataRequest).</summary>
    public sealed class TonWalletSignDataRequest
    {
        private readonly TonWalletKit _kit;

        /// <summary>The request, including what will be signed.</summary>
        public TONSignDataRequestEvent Event { get; }

        /// <summary>Set when the request came embedded in a connect approval.</summary>
        public TONEmbeddedSignDataRequestEvent EmbeddedEvent { get; }

        internal TonWalletSignDataRequest(TonWalletKit kit, TONSignDataRequestEvent request)
        {
            _kit = kit;
            Event = request;
        }

        internal TonWalletSignDataRequest(TonWalletKit kit, TONEmbeddedSignDataRequestEvent embedded)
        {
            _kit = kit;
            EmbeddedEvent = embedded;
        }

        /// <summary>Signs the data.</summary>
        public Task<TONSignDataApprovalResponse> ApproveAsync(TONSignDataApprovalResponse response = null)
        {
            return EmbeddedEvent != null
                ? _kit.InvokeAsync<TONSignDataApprovalResponse>(
                      "approveSignDataRequest", TonJson.Args(EmbeddedEvent, response))
                : _kit.InvokeAsync<TONSignDataApprovalResponse>(
                      "approveSignDataRequest", TonJson.Args(Event, response));
        }

        /// <summary>Refuses to sign.</summary>
        public Task RejectAsync(string reason = null)
        {
            return EmbeddedEvent != null
                ? _kit.CallAsync("rejectSignDataRequest", TonJson.Args(EmbeddedEvent, reason))
                : _kit.CallAsync("rejectSignDataRequest", TonJson.Args(Event, reason));
        }
    }
}
