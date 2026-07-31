# Ton.WalletKit.Api.Model.TONTransactionTraceActionJettonSwapDetails
Details of a Jetton swap action on a DEX.

## Properties

Name | Type | Description | Notes
------------ | ------------- | ------------- | -------------
**Dex** | **string** | Name of the decentralized exchange | 
**PeerSwaps** | **List&lt;Object&gt;** | Related peer swap operations (for multi-hop swaps) | 
**DexIncomingTransfer** | [**TONTransactionTraceActionJettonTransfer**](TONTransactionTraceActionJettonTransfer.md) |  | [optional] 
**DexOutgoingTransfer** | [**TONTransactionTraceActionJettonTransfer**](TONTransactionTraceActionJettonTransfer.md) |  | [optional] 
**Sender** | **string** | User-friendly TON address representation (e.g., \&quot;EQDtFpEwcFAEcRe5mLVh2N6C0x-_hJEM7W61_JLnSF74p4q2\&quot;) | [optional] 

[[Back to Model list]](../../README.md#documentation-for-models) [[Back to API list]](../../README.md#documentation-for-api-endpoints) [[Back to README]](../../README.md)

