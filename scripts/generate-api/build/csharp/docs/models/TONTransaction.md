# Ton.WalletKit.Api.Model.TONTransaction
Transaction on the TON blockchain.

## Properties

Name | Type | Description | Notes
------------ | ------------- | ------------- | -------------
**Account** | **string** | User-friendly TON address representation (e.g., \&quot;EQDtFpEwcFAEcRe5mLVh2N6C0x-_hJEM7W61_JLnSF74p4q2\&quot;) | 
**IsEmulated** | **bool** | Emulated state of the transaction | 
**LogicalTime** | **string** | Logical time value used for ordering transactions on the TON blockchain | 
**McBlockSeqno** | **decimal** | Masterchain block sequence number | 
**Now** | **decimal** | Unix timestamp of the transaction | 
**OutMessages** | [**List&lt;TONTransactionMessage&gt;**](TONTransactionMessage.md) | The list of outgoing messages produced by the transaction | 
**AccountStateAfter** | [**TONTransactionAccountState**](TONTransactionAccountState.md) |  | [optional] 
**AccountStateBefore** | [**TONTransactionAccountState**](TONTransactionAccountState.md) |  | [optional] 
**BlockRef** | [**TONTransactionBlockRef**](TONTransactionBlockRef.md) |  | [optional] 
**Description** | [**TONTransactionDescription**](TONTransactionDescription.md) |  | [optional] 
**EndStatus** | **TONAccountStatus** |  | [optional] 
**InMessage** | [**TONTransactionMessage**](TONTransactionMessage.md) |  | [optional] 
**OrigStatus** | **TONAccountStatus** |  | [optional] 
**PreviousTransactionHash** | **string** | The hash of the previous transaction | [optional] 
**PreviousTransactionLogicalTime** | **string** | Logical time value used for ordering transactions on the TON blockchain | [optional] 
**TotalFees** | **string** | Token amount represented as a string to preserve precision. For TON, this is typically in nanotons (1 TON &#x3D; 10^9 nanotons). | [optional] 
**TotalFeesExtraCurrencies** | **Dictionary&lt;string, string&gt;** | Map of extra currency IDs to their amounts. Extra currencies are additional tokens that can be attached to TON messages. | [optional] 
**TraceId** | **string** | ID of the trace | [optional] 

[[Back to Model list]](../../README.md#documentation-for-models) [[Back to API list]](../../README.md#documentation-for-api-endpoints) [[Back to README]](../../README.md)

