# Ton.WalletKit.Api.Model.TONTransactionEmulatedTrace
Extended transaction trace with emulation-specific data including code/data cells and address metadata.

## Properties

Name | Type | Description | Notes
------------ | ------------- | ------------- | -------------
**Actions** | [**List&lt;TONTransactionTraceAction&gt;**](TONTransactionTraceAction.md) | List of high-level actions extracted from the trace | 
**AddressBook** | [**Dictionary&lt;string, TONAddressBookEntry&gt;**](TONAddressBookEntry.md) | Map of raw addresses to their metadata entries. | 
**CodeCells** | **Dictionary&lt;string, string&gt;** | Map of code cell hashes to their Base64-encoded content | 
**DataCells** | **Dictionary&lt;string, string&gt;** | Map of data cell hashes to their Base64-encoded content | 
**IsIncomplete** | **bool** | Whether the trace is incomplete due to limits or errors | 
**McBlockSeqno** | **decimal** | Masterchain block sequence number where emulation was performed | 
**Metadata** | [**Dictionary&lt;string, TONTransactionAddressMetadataEntry&gt;**](TONTransactionAddressMetadataEntry.md) | Metadata about addresses, including indexing and associated token info. | 
**Trace** | [**TONTransactionTraceNode**](TONTransactionTraceNode.md) |  | 
**Transactions** | [**Dictionary&lt;string, TONTransaction&gt;**](TONTransaction.md) | Map of transaction hashes to transaction details | 

[[Back to Model list]](../../README.md#documentation-for-models) [[Back to API list]](../../README.md#documentation-for-api-endpoints) [[Back to README]](../../README.md)

