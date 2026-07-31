# Ton.WalletKit.Api.Model.TONEmulationTransaction
Transaction within an emulated trace.

## Properties

Name | Type | Description | Notes
------------ | ------------- | ------------- | -------------
**Account** | **string** | User-friendly TON address representation (e.g., \&quot;EQDtFpEwcFAEcRe5mLVh2N6C0x-_hJEM7W61_JLnSF74p4q2\&quot;) | 
**AccountStateAfter** | [**TONEmulationAccountState**](TONEmulationAccountState.md) |  | 
**AccountStateBefore** | [**TONEmulationAccountState**](TONEmulationAccountState.md) |  | 
**BlockRef** | [**TONEmulationBlockRef**](TONEmulationBlockRef.md) |  | 
**Description** | [**TONEmulationTransactionDescription**](TONEmulationTransactionDescription.md) |  | 
**EndStatus** | **TONAccountStatus** |  | 
**IsEmulated** | **bool** | Whether this transaction was produced by emulation rather than executed on-chain | 
**Lt** | **string** | Logical time value used for ordering transactions on the TON blockchain | 
**McBlockSeqno** | **decimal** | Masterchain block sequence number | 
**Now** | **decimal** | Unix timestamp of the transaction | 
**OrigStatus** | **TONAccountStatus** |  | 
**OutMsgs** | [**List&lt;TONEmulationMessage&gt;**](TONEmulationMessage.md) | Outgoing messages produced by this transaction | 
**TotalFees** | **string** | Token amount represented as a string to preserve precision. For TON, this is typically in nanotons (1 TON &#x3D; 10^9 nanotons). | 
**TotalFeesExtraCurrencies** | **Dictionary&lt;string, string&gt;** | Map of extra currency IDs to their amounts. Extra currencies are additional tokens that can be attached to TON messages. | 
**InMsg** | [**TONEmulationMessage**](TONEmulationMessage.md) |  | [optional] 
**PrevTransLt** | **string** | Logical time value used for ordering transactions on the TON blockchain | [optional] 
**TraceId** | **string** | Trace identifier, if available | [optional] 

[[Back to Model list]](../../README.md#documentation-for-models) [[Back to API list]](../../README.md#documentation-for-api-endpoints) [[Back to README]](../../README.md)

