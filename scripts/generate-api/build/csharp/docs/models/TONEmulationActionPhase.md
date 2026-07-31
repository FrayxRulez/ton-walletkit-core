# Ton.WalletKit.Api.Model.TONEmulationActionPhase
Action phase of transaction execution (outgoing message sending).

## Properties

Name | Type | Description | Notes
------------ | ------------- | ------------- | -------------
**HasNoFunds** | **bool** | Whether the transaction failed due to insufficient funds | 
**IsSuccess** | **bool** | Whether the action phase succeeded | 
**IsValid** | **bool** | Whether the action list was valid | 
**MsgsCreated** | **decimal** | Number of messages created | 
**ResultCode** | **decimal** | Result code of the action phase | 
**SkippedActions** | **decimal** | Number of actions skipped | 
**SpecActions** | **decimal** | Number of special actions executed | 
**StatusChange** | **string** | Account status change applied during the action phase | 
**TotalActions** | **decimal** | Total number of actions processed | 
**TotalMsgSize** | [**TONEmulationActionMessageSize**](TONEmulationActionMessageSize.md) |  | 
**TotalActionFees** | **string** | Token amount represented as a string to preserve precision. For TON, this is typically in nanotons (1 TON &#x3D; 10^9 nanotons). | [optional] 
**TotalFwdFees** | **string** | Token amount represented as a string to preserve precision. For TON, this is typically in nanotons (1 TON &#x3D; 10^9 nanotons). | [optional] 

[[Back to Model list]](../../README.md#documentation-for-models) [[Back to API list]](../../README.md#documentation-for-api-endpoints) [[Back to README]](../../README.md)

