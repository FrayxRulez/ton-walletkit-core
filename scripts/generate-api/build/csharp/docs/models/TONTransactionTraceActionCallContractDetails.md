# Ton.WalletKit.Api.Model.TONTransactionTraceActionCallContractDetails

## Properties

Name | Type | Description | Notes
------------ | ------------- | ------------- | -------------
**Opcode** | **string** | Opcode or method identifier of the contract call. | 
**Destination** | **string** | User-friendly TON address representation (e.g., \&quot;EQDtFpEwcFAEcRe5mLVh2N6C0x-_hJEM7W61_JLnSF74p4q2\&quot;) | [optional] 
**Source** | **string** | User-friendly TON address representation (e.g., \&quot;EQDtFpEwcFAEcRe5mLVh2N6C0x-_hJEM7W61_JLnSF74p4q2\&quot;) | [optional] 
**Value** | **string** | Token amount represented as a string to preserve precision. For TON, this is typically in nanotons (1 TON &#x3D; 10^9 nanotons). | [optional] 
**ValueExtraCurrencies** | **Dictionary&lt;string, string&gt;** | Map of extra currency IDs to their amounts. Extra currencies are additional tokens that can be attached to TON messages. | [optional] 

[[Back to Model list]](../../README.md#documentation-for-models) [[Back to API list]](../../README.md#documentation-for-api-endpoints) [[Back to README]](../../README.md)

