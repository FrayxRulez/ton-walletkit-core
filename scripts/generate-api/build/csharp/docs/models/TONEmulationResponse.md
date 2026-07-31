# Ton.WalletKit.Api.Model.TONEmulationResponse
Unified emulation response model, normalised from either Toncenter or TonAPI sources.

## Properties

Name | Type | Description | Notes
------------ | ------------- | ------------- | -------------
**Actions** | [**List&lt;TONEmulationAction&gt;**](TONEmulationAction.md) | High-level actions extracted from the trace | 
**AddressBook** | [**Dictionary&lt;string, TONEmulationAddressBookEntry&gt;**](TONEmulationAddressBookEntry.md) | Address book mapping raw addresses to human-readable metadata | 
**CodeCells** | **Dictionary&lt;string, string&gt;** | Map of code cell hashes to their BOC base64 representations | 
**DataCells** | **Dictionary&lt;string, string&gt;** | Map of data cell hashes to their BOC base64 representations | 
**IsIncomplete** | **bool** | Whether the trace is incomplete due to limits or errors | 
**McBlockSeqno** | **decimal** | Masterchain block sequence number used during emulation | 
**Trace** | [**TONEmulationTraceNode**](TONEmulationTraceNode.md) |  | 
**Transactions** | [**Dictionary&lt;string, TONEmulationTransaction&gt;**](TONEmulationTransaction.md) | Map of transaction hashes to transaction details | 

[[Back to Model list]](../../README.md#documentation-for-models) [[Back to API list]](../../README.md#documentation-for-api-endpoints) [[Back to README]](../../README.md)

