# Ton.WalletKit.Api.Model.TONTransactionTraceMoneyFlow
Summary of token flows for a transaction.

## Properties

Name | Type | Description | Notes
------------ | ------------- | ------------- | -------------
**AllJettonTransfers** | [**List&lt;TONTransactionTraceMoneyFlowItem&gt;**](TONTransactionTraceMoneyFlowItem.md) | List of all token transfers involved in the transaction | 
**Inputs** | **string** | Token amount represented as a string to preserve precision. For TON, this is typically in nanotons (1 TON &#x3D; 10^9 nanotons). | 
**OurTransfers** | [**List&lt;TONTransactionTraceMoneyFlowItem&gt;**](TONTransactionTraceMoneyFlowItem.md) | List of token transfers involving our address | 
**Outputs** | **string** | Token amount represented as a string to preserve precision. For TON, this is typically in nanotons (1 TON &#x3D; 10^9 nanotons). | 
**OurAddress** | **string** | User-friendly TON address representation (e.g., \&quot;EQDtFpEwcFAEcRe5mLVh2N6C0x-_hJEM7W61_JLnSF74p4q2\&quot;) | [optional] 

[[Back to Model list]](../../README.md#documentation-for-models) [[Back to API list]](../../README.md#documentation-for-api-endpoints) [[Back to README]](../../README.md)

