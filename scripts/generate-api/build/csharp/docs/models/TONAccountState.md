# Ton.WalletKit.Api.Model.TONAccountState
Blockchain state of an account at a given point in time.  The `status` field distinguishes four cases: - `active` — contract deployed, `code` and `data` present - `uninitialized` — has balance/history but no contract deployed; `code`/`data` omitted - `frozen` — frozen due to storage debt; `frozenHash` points at the pre-freeze state - `non-existing` — no on-chain record at all; balance is `'0'` and other fields omitted

## Properties

Name | Type | Description | Notes
------------ | ------------- | ------------- | -------------
**Address** | **string** | User-friendly TON address representation (e.g., \&quot;EQDtFpEwcFAEcRe5mLVh2N6C0x-_hJEM7W61_JLnSF74p4q2\&quot;) | 
**Balance** | **string** | Balance formatted in TON (10^9 nanotons &#x3D; 1 TON). | 
**ExtraCurrencies** | **Dictionary&lt;string, string&gt;** | Map of extra currency IDs to their amounts. Extra currencies are additional tokens that can be attached to TON messages. | 
**RawBalance** | **string** | Token amount represented as a string to preserve precision. For TON, this is typically in nanotons (1 TON &#x3D; 10^9 nanotons). | 
**Status** | **TONAccountStatus** |  | 
**Code** | **string** | Base64-encoded contract code BOC. Omitted if the contract is not deployed. | [optional] 
**Data** | **string** | Base64-encoded contract data BOC. Omitted if the contract is not deployed. | [optional] 
**LastTransaction** | [**TONTransactionId**](TONTransactionId.md) |  | [optional] 

[[Back to Model list]](../../README.md#documentation-for-models) [[Back to API list]](../../README.md#documentation-for-api-endpoints) [[Back to README]](../../README.md)

