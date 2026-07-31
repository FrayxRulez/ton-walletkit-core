# Ton.WalletKit.Api.Model.TONTransactionAction
Action phase of transaction execution (message sending).

## Properties

Name | Type | Description | Notes
------------ | ------------- | ------------- | -------------
**HasNoFunds** | **bool** | The flag indicating if the transaction had insufficient funds | [optional] 
**IsSuccess** | **bool** | The flag indicating whether the action phase succeeded | [optional] 
**IsValid** | **bool** | The flag indicating whether the action phase was valid | [optional] 
**MessagesCreatedNumber** | **decimal** | The number of messages created in the action phase | [optional] 
**ResultCode** | **decimal** | The result code returned from the action phase | [optional] 
**SkippedActionsNumber** | **decimal** | The number of skipped actions during execution | [optional] 
**SpecActionsNumber** | **decimal** | The number of special actions executed | [optional] 
**StatusChange** | **string** | The status change applied to the account during the action phase | [optional] 
**TotalActionFees** | **string** | Token amount represented as a string to preserve precision. For TON, this is typically in nanotons (1 TON &#x3D; 10^9 nanotons). | [optional] 
**TotalActionsNumber** | **decimal** | The total number of actions processed | [optional] 
**TotalForwardingFees** | **string** | Token amount represented as a string to preserve precision. For TON, this is typically in nanotons (1 TON &#x3D; 10^9 nanotons). | [optional] 
**TotalMessagesSize** | [**TONTransactionActionMessageSize**](TONTransactionActionMessageSize.md) |  | [optional] 

[[Back to Model list]](../../README.md#documentation-for-models) [[Back to API list]](../../README.md#documentation-for-api-endpoints) [[Back to README]](../../README.md)

