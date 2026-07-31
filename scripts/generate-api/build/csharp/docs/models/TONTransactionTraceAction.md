# Ton.WalletKit.Api.Model.TONTransactionTraceAction
High-level action extracted from a transaction trace.

## Properties

Name | Type | Description | Notes
------------ | ------------- | ------------- | -------------
**Accounts** | **List&lt;string&gt;** | List of accounts involved in this action | 
**Details** | [**TONTransactionTraceActionDetails**](TONTransactionTraceActionDetails.md) |  | 
**Transactions** | **List&lt;Object&gt;** | List of transaction hashes involved in this action | 
**ActionId** | **string** | Action identifier | [optional] 
**EndLt** | **string** | Logical time value used for ordering transactions on the TON blockchain | [optional] 
**EndUtime** | **decimal** | Unix time when the action ended | [optional] 
**IsSuccess** | **bool** | Indicates if the action was successful | [optional] 
**StartLt** | **string** | Logical time value used for ordering transactions on the TON blockchain | [optional] 
**StartUtime** | **decimal** | Unix time when the action started | [optional] 
**TraceEndLt** | **string** | Logical time value used for ordering transactions on the TON blockchain | [optional] 
**TraceEndUtime** | **decimal** | Unix time when the trace ended | [optional] 
**TraceId** | **string** | Trace identifier | [optional] 
**TraceMcSeqnoEnd** | **decimal** | Masterchain block sequence number when the trace ended | [optional] 

[[Back to Model list]](../../README.md#documentation-for-models) [[Back to API list]](../../README.md#documentation-for-api-endpoints) [[Back to README]](../../README.md)

