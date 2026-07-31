//
// The wallet object model, mirroring kit-ios method for method.
//
// kit-ios splits this in two: TONWalletAdapterProtocol is what a key can do
// (sign, derive an address), and TONWalletProtocol adds what a *registered*
// wallet can do (read chain state, build transfers). A wallet is an adapter, so
// the same split holds here.
//
using System.Collections.Generic;
using System.Threading.Tasks;
using Ton.WalletKit.Api.Model;

namespace Ton.WalletKit
{
    /// <summary>A key that can sign (kit-ios TONWalletSignerProtocol).</summary>
    public interface ITonWalletSigner
    {
        /// <summary>Public key, 0x-prefixed hex.</summary>
        string PublicKey { get; }

        /// <summary>Signs raw bytes; returns a hex signature.</summary>
        Task<string> SignAsync(byte[] data);
    }

    /// <summary>
    /// A wallet contract bound to a signer (kit-ios TONWalletAdapterProtocol).
    /// It knows its address before it is registered with the kit, and it can
    /// sign — but it cannot read the chain until it becomes an <see cref="ITonWallet"/>.
    /// </summary>
    public interface ITonWalletAdapter
    {
        /// <summary>The wallet id this adapter derives (kit-ios identifier()).</summary>
        string Identifier { get; }

        /// <summary>Public key, 0x-prefixed hex.</summary>
        string PublicKey { get; }

        /// <summary>The network this wallet lives on.</summary>
        TONNetwork Network { get; }

        /// <summary>User-friendly address (UQ…/EQ…).</summary>
        string Address { get; }

        /// <summary>Deployment state init, a base64 BOC.</summary>
        Task<string> StateInitAsync();

        /// <summary>
        /// Signs a transaction and returns the BOC.
        /// <see cref="TONSignedSendTransactionOptions.FakeSignature"/> produces a
        /// correctly shaped but unusable signature — right for fee estimation, and
        /// what tests use so nothing can be broadcast by accident.
        /// </summary>
        Task<string> SignedSendTransactionAsync(TONTransactionRequest input,
                                                TONSignedSendTransactionOptions options = null);

        /// <summary>Signs a transaction as a TON Connect signMessage response.</summary>
        Task<string> SignedSignMessageAsync(TONTransactionRequest input,
                                            TONSignedSendTransactionOptions options = null);

        /// <summary>Signs prepared sign-data; returns a hex signature.</summary>
        Task<string> SignedSignDataAsync(TONPreparedSignData input, bool? fakeSignature = null);

        /// <summary>Signs a ton_proof message; returns a hex signature.</summary>
        Task<string> SignedTonProofAsync(TONProofMessage input, bool? fakeSignature = null);

        /// <summary>TON Connect features this wallet advertises.</summary>
        Task<IReadOnlyList<TonFeature>> SupportedFeaturesAsync();
    }

    /// <summary>
    /// A wallet registered with the kit (kit-ios TONWalletProtocol).
    ///
    /// This is a handle, not storage: walletkit keeps wallets in memory only, so
    /// after a restart the app rebuilds them from the persisted mnemonic.
    /// </summary>
    public interface ITonWallet : ITonWalletAdapter
    {
        /// <summary>The kit's wallet id (a base64 hash, not network:address).</summary>
        string Id { get; }

        /// <summary>Current balance, in nanotons.</summary>
        Task<TonAmount> BalanceAsync();

        /// <summary>
        /// Builds an unsigned TON transfer. The recipient must be in
        /// user-friendly form (UQ…/EQ…): a raw address passes the early check and
        /// is then rejected with a message that names the transaction, not the
        /// address.
        /// </summary>
        Task<TONTransactionRequest> TransferTonTransactionAsync(TONTransferRequest request);

        /// <summary>Builds one transaction carrying several transfers.</summary>
        Task<TONTransactionRequest> TransferTonTransactionAsync(IEnumerable<TONTransferRequest> requests);

        /// <summary>Signs and broadcasts. This spends real funds.</summary>
        Task<TONSendTransactionResponse> SendAsync(TONTransactionRequest transactionRequest);

        /// <summary>Emulates a transaction: fees, balance changes, actions.</summary>
        Task<TONTransactionEmulatedPreview> PreviewAsync(TONTransactionRequest transactionRequest,
                                                         TONTransactionPreviewOptions options = null);

        /// <summary>Builds an NFT transfer.</summary>
        Task<TONTransactionRequest> TransferNftTransactionAsync(TONNFTTransferRequest request);

        /// <summary>Builds an NFT transfer from a raw message.</summary>
        Task<TONTransactionRequest> TransferNftTransactionAsync(TONNFTRawTransferRequest request);

        /// <summary>NFTs this wallet holds.</summary>
        Task<TONNFTsResponse> NftsAsync(TONNFTsRequest request = null);

        /// <summary>One NFT by address, or null when the wallet does not hold it.</summary>
        Task<TONNFT> NftAsync(string address);

        /// <summary>Balance of one jetton, in its own smallest unit.</summary>
        Task<TonAmount> JettonBalanceAsync(string jettonAddress);

        /// <summary>This wallet's jetton wallet address for a jetton master.</summary>
        Task<string> JettonWalletAddressAsync(string jettonAddress);

        /// <summary>Builds a jetton transfer.</summary>
        Task<TONTransactionRequest> TransferJettonTransactionAsync(TONJettonsTransferRequest request);

        /// <summary>Jettons this wallet holds.</summary>
        Task<TONJettonsResponse> JettonsAsync(TONJettonsRequest request = null);
    }

    /// <summary>A generated mnemonic and the wallet built from it (kit-ios TONWalletCreationResult).</summary>
    public sealed class TonWalletCreationResult
    {
        /// <summary>The words. Persist these: walletkit stores no keys.</summary>
        public TonMnemonic Mnemonic { get; }

        /// <summary>The adapter derived from them; register it with AddAsync.</summary>
        public ITonWalletAdapter WalletAdapter { get; }

        /// <summary>Pairs a mnemonic with the adapter derived from it.</summary>
        public TonWalletCreationResult(TonMnemonic mnemonic, ITonWalletAdapter walletAdapter)
        {
            Mnemonic = mnemonic;
            WalletAdapter = walletAdapter;
        }
    }

    /// <summary>
    /// A TON mnemonic (kit-ios TONMnemonic).
    ///
    /// This is the only thing that can rebuild a wallet, and the core never
    /// persists it — storing it securely is the app's job.
    /// </summary>
    public sealed class TonMnemonic
    {
        /// <summary>The words, in order.</summary>
        public IReadOnlyList<string> Value { get; }

        /// <summary>Wraps a word list.</summary>
        public TonMnemonic(IReadOnlyList<string> value)
        {
            Value = value;
        }

        /// <summary>The words separated by spaces, the usual storage form.</summary>
        public override string ToString()
        {
            return Value == null ? string.Empty : string.Join(" ", Value);
        }

        /// <summary>Parses a space-separated mnemonic.</summary>
        public static TonMnemonic Parse(string value)
        {
            return new TonMnemonic(value == null
                ? new string[0]
                : value.Split(new[] { ' ', '\t', '\n', '\r' }, System.StringSplitOptions.RemoveEmptyEntries));
        }
    }
}
