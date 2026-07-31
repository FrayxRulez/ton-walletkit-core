# Ton.WalletKit.Api.Model.TONTransactionsUpdate

## Properties

Name | Type | Description | Notes
------------ | ------------- | ------------- | -------------
**Address** | **string** | User-friendly TON address representation (e.g., \&quot;EQDtFpEwcFAEcRe5mLVh2N6C0x-_hJEM7W61_JLnSF74p4q2\&quot;) | 
**Status** | **TONStreamingUpdateStatus** |  | 
**Transactions** | [**List&lt;TONTransaction&gt;**](TONTransaction.md) | The array of transactions | 
**Type** | **string** | The update type field | 
**AddressBook** | [**Dictionary&lt;string, TONAddressBookEntry&gt;**](TONAddressBookEntry.md) | Map of raw addresses to their metadata entries. | [optional] 
**Metadata** | [**Dictionary&lt;string, TONTransactionAddressMetadataEntry&gt;**](TONTransactionAddressMetadataEntry.md) | Metadata about addresses, including indexing and associated token info. | [optional] 

[[Back to Model list]](../../README.md#documentation-for-models) [[Back to API list]](../../README.md#documentation-for-api-endpoints) [[Back to README]](../../README.md)

