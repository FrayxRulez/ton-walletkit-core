# Ton.WalletKit.Api.Model.TONTransactionTrace
Trace of a transaction execution showing the full message chain.

## Properties

Name | Type | Description | Notes
------------ | ------------- | ------------- | -------------
**Actions** | [**List&lt;TONTransactionTraceAction&gt;**](TONTransactionTraceAction.md) | List of high-level actions extracted from the trace | 
**IsIncomplete** | **bool** | Whether the trace is incomplete due to limits or errors | 
**McBlockSeqno** | **decimal** | Masterchain block sequence number where emulation was performed | 
**Trace** | [**TONTransactionTraceNode**](TONTransactionTraceNode.md) |  | 
**Transactions** | [**Dictionary&lt;string, TONTransaction&gt;**](TONTransaction.md) | Map of transaction hashes to transaction details | 

[[Back to Model list]](../../README.md#documentation-for-models) [[Back to API list]](../../README.md#documentation-for-api-endpoints) [[Back to README]](../../README.md)

