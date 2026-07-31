# Ton.WalletKit.Api.Model.TONEmulationAction
High-level action extracted from an emulated transaction trace.

## Properties

Name | Type | Description | Notes
------------ | ------------- | ------------- | -------------
**Accounts** | **List&lt;string&gt;** | Addresses of accounts involved in this action | 
**Details** | **Dictionary&lt;string, Object&gt;** | Action-specific detail fields keyed by name | 
**EndLt** | **string** | Logical time value used for ordering transactions on the TON blockchain | 
**EndUtime** | **decimal** | Unix timestamp when the action ended | 
**IsSuccess** | **bool** | Whether the action completed successfully | 
**StartLt** | **string** | Logical time value used for ordering transactions on the TON blockchain | 
**StartUtime** | **decimal** | Unix timestamp when the action started | 
**TraceEndLt** | **string** | Logical time value used for ordering transactions on the TON blockchain | 
**TraceEndUtime** | **decimal** | Unix timestamp when the trace ended | 
**TraceMcSeqnoEnd** | **decimal** | Masterchain block sequence number when the trace ended | 
**Transactions** | **List&lt;Object&gt;** | Hex-encoded hashes of transactions involved in this action | 
**Type** | **string** | Action type identifier (e.g. \&quot;jetton_transfer\&quot;, \&quot;ton_transfer\&quot;, \&quot;jetton_swap\&quot;) | 
**TraceId** | **string** | Trace identifier this action belongs to | [optional] 

[[Back to Model list]](../../README.md#documentation-for-models) [[Back to API list]](../../README.md#documentation-for-api-endpoints) [[Back to README]](../../README.md)

