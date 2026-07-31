//
// The wallet objects the kit hands out.
//
// Each one is a thin handle: an id the ABI addresses, plus the fields kit-ios
// exposes synchronously (address, public key, network), captured when the object
// was created. Everything else is a call into the core.
//
using System.Collections.Generic;
using System.Threading.Tasks;
using Ton.WalletKit.Api.Model;

namespace Ton.WalletKit
{
    /// <summary>A signer living in the core; the secret never crosses the ABI.</summary>
    internal sealed class TonWalletSigner : ITonWalletSigner
    {
        private readonly TonWalletKit _kit;

        /// <summary>The id the ABI addresses this signer by.</summary>
        public string SignerId { get; }

        /// <inheritdoc/>
        public string PublicKey { get; }

        internal TonWalletSigner(TonWalletKit kit, TonSignerDescriptor descriptor)
        {
            _kit = kit;
            SignerId = descriptor.SignerId;
            PublicKey = descriptor.PublicKey;
        }

        /// <inheritdoc/>
        public Task<string> SignAsync(byte[] data)
        {
            // A byte array, as kit-ios passes [UInt8]: the ABI is JSON, so there
            // is no binary form to hand over.
            return _kit.InvokeAsync<string>("sign", TonJson.Args(SignerId, data));
        }
    }

    /// <summary>
    /// A wallet contract bound to a signer. Addressed by an adapter id until it
    /// is registered, and by its walletId afterwards — the core accepts either,
    /// because a registered wallet is also an adapter.
    /// </summary>
    internal class TonWalletAdapter : ITonWalletAdapter
    {
        /// <summary>The kit this object belongs to.</summary>
        protected TonWalletKit Kit { get; }

        /// <summary>
        /// The id the ABI addresses this object by: the adapter id until the
        /// wallet is registered, its walletId afterwards.
        /// </summary>
        internal string Handle { get; }

        /// <inheritdoc/>
        public string Identifier { get; }

        /// <inheritdoc/>
        public string PublicKey { get; }

        /// <inheritdoc/>
        public TONNetwork Network { get; }

        /// <inheritdoc/>
        public string Address { get; }

        internal TonWalletAdapter(TonWalletKit kit, TonAdapterDescriptor descriptor)
        {
            Kit = kit;
            Handle = descriptor.AdapterId;
            Identifier = descriptor.WalletId;
            PublicKey = descriptor.PublicKey;
            Network = descriptor.Network;
            Address = descriptor.Address;
        }

        internal TonWalletAdapter(TonWalletKit kit, TonWalletDescriptor descriptor)
        {
            Kit = kit;
            Handle = descriptor.WalletId;
            Identifier = descriptor.WalletId;
            PublicKey = descriptor.PublicKey;
            Network = descriptor.Network;
            Address = descriptor.Address;
        }

        /// <inheritdoc/>
        public Task<string> StateInitAsync()
        {
            return Kit.InvokeAsync<string>("getStateInit", TonJson.Args(Handle));
        }

        /// <inheritdoc/>
        public Task<string> SignedSendTransactionAsync(TONTransactionRequest input,
                                                       TONSignedSendTransactionOptions options = null)
        {
            return Kit.InvokeAsync<string>("getSignedSendTransaction", TonJson.Args(Handle, input, options));
        }

        /// <inheritdoc/>
        public Task<string> SignedSignMessageAsync(TONTransactionRequest input,
                                                   TONSignedSendTransactionOptions options = null)
        {
            return Kit.InvokeAsync<string>("getSignedSignMessage", TonJson.Args(Handle, input, options));
        }

        /// <inheritdoc/>
        public Task<string> SignedSignDataAsync(TONPreparedSignData input, bool? fakeSignature = null)
        {
            return Kit.InvokeAsync<string>("getSignedSignData", TonJson.Args(Handle, input, Options(fakeSignature)));
        }

        /// <inheritdoc/>
        public Task<string> SignedTonProofAsync(TONProofMessage input, bool? fakeSignature = null)
        {
            return Kit.InvokeAsync<string>("getSignedTonProof", TonJson.Args(Handle, input, Options(fakeSignature)));
        }

        /// <inheritdoc/>
        public async Task<IReadOnlyList<TonFeature>> SupportedFeaturesAsync()
        {
            var features = await Kit.InvokeAsync<List<TonFeature>>("getSupportedFeatures", TonJson.Args(Handle))
                                    .ConfigureAwait(false);
            return features ?? (IReadOnlyList<TonFeature>)new TonFeature[0];
        }

        private static TONSignedSendTransactionOptions Options(bool? fakeSignature)
        {
            return fakeSignature.HasValue
                ? new TONSignedSendTransactionOptions(fakeSignature.Value)
                : null;
        }
    }

    /// <summary>A wallet registered with the kit.</summary>
    internal sealed class TonWallet : TonWalletAdapter, ITonWallet
    {
        /// <inheritdoc/>
        public string Id { get; }

        /// <summary>The contract version, when the core reported one.</summary>
        public TonWalletVersion Version { get; }

        internal TonWallet(TonWalletKit kit, TonWalletDescriptor descriptor) : base(kit, descriptor)
        {
            Id = descriptor.WalletId;
            Version = descriptor.Version;
        }

        /// <inheritdoc/>
        public async Task<TonAmount> BalanceAsync()
        {
            string balance = await Kit.InvokeAsync<string>("getBalance", TonJson.Args(Id)).ConfigureAwait(false);
            return TonAmount.FromNanotons(balance);
        }

        /// <inheritdoc/>
        public Task<TONTransactionRequest> TransferTonTransactionAsync(TONTransferRequest request)
        {
            return Kit.InvokeAsync<TONTransactionRequest>("createTransferTonTransaction",
                                                          TonJson.Args(Id, request));
        }

        /// <inheritdoc/>
        public Task<TONTransactionRequest> TransferTonTransactionAsync(IEnumerable<TONTransferRequest> requests)
        {
            return Kit.InvokeAsync<TONTransactionRequest>("createTransferMultiTonTransaction",
                                                          TonJson.Args(Id, new List<TONTransferRequest>(requests)));
        }

        /// <inheritdoc/>
        public Task<TONSendTransactionResponse> SendAsync(TONTransactionRequest transactionRequest)
        {
            return Kit.InvokeAsync<TONSendTransactionResponse>("sendTransaction",
                                                               TonJson.Args(Id, transactionRequest));
        }

        /// <inheritdoc/>
        public Task<TONTransactionEmulatedPreview> PreviewAsync(TONTransactionRequest transactionRequest,
                                                                TONTransactionPreviewOptions options = null)
        {
            return Kit.InvokeAsync<TONTransactionEmulatedPreview>("getTransactionPreview",
                                                                   TonJson.Args(Id, transactionRequest, options));
        }

        /// <inheritdoc/>
        public Task<TONTransactionRequest> TransferNftTransactionAsync(TONNFTTransferRequest request)
        {
            return Kit.InvokeAsync<TONTransactionRequest>("createTransferNftTransaction", TonJson.Args(Id, request));
        }

        /// <inheritdoc/>
        public Task<TONTransactionRequest> TransferNftTransactionAsync(TONNFTRawTransferRequest request)
        {
            return Kit.InvokeAsync<TONTransactionRequest>("createTransferNftRawTransaction",
                                                          TonJson.Args(Id, request));
        }

        /// <inheritdoc/>
        public Task<TONNFTsResponse> NftsAsync(TONNFTsRequest request = null)
        {
            return Kit.InvokeAsync<TONNFTsResponse>("getNfts", TonJson.Args(Id, request));
        }

        /// <inheritdoc/>
        public Task<TONNFT> NftAsync(string address)
        {
            return Kit.InvokeAsync<TONNFT>("getNft", TonJson.Args(Id, address));
        }

        /// <inheritdoc/>
        public async Task<TonAmount> JettonBalanceAsync(string jettonAddress)
        {
            string balance = await Kit.InvokeAsync<string>("getJettonBalance", TonJson.Args(Id, jettonAddress))
                                      .ConfigureAwait(false);
            return TonAmount.FromNanotons(balance);
        }

        /// <inheritdoc/>
        public Task<string> JettonWalletAddressAsync(string jettonAddress)
        {
            return Kit.InvokeAsync<string>("getJettonWalletAddress", TonJson.Args(Id, jettonAddress));
        }

        /// <inheritdoc/>
        public Task<TONTransactionRequest> TransferJettonTransactionAsync(TONJettonsTransferRequest request)
        {
            return Kit.InvokeAsync<TONTransactionRequest>("createTransferJettonTransaction",
                                                          TonJson.Args(Id, request));
        }

        /// <inheritdoc/>
        public Task<TONJettonsResponse> JettonsAsync(TONJettonsRequest request = null)
        {
            return Kit.InvokeAsync<TONJettonsResponse>("getJettons", TonJson.Args(Id, request));
        }
    }
}
