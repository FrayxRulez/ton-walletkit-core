# Ton.WalletKit.Api.Model.TONTransactionDescription
Detailed description of transaction execution phases.

## Properties

Name | Type | Description | Notes
------------ | ------------- | ------------- | -------------
**IsAborted** | **bool** | The flag indicating if the transaction was aborted | 
**IsCreditFirst** | **bool** | The flag indicating if the credit phase was executed first | 
**IsDestroyed** | **bool** | The flag indicating if the account was destroyed | 
**IsInstalled** | **bool** | The flag indicating if the contract was installed | 
**IsTock** | **bool** | The flag indicating if this was a tock transaction | 
**Type** | **string** | The transaction type (e.g., tick-tock, ord, split-prepare) | 
**Action** | [**TONTransactionAction**](TONTransactionAction.md) |  | [optional] 
**ComputePhase** | [**TONTransactionComputePhase**](TONTransactionComputePhase.md) |  | [optional] 
**CreditPhase** | [**TONTransactionCreditPhase**](TONTransactionCreditPhase.md) |  | [optional] 
**StoragePhase** | [**TONTransactionStoragePhase**](TONTransactionStoragePhase.md) |  | [optional] 

[[Back to Model list]](../../README.md#documentation-for-models) [[Back to API list]](../../README.md#documentation-for-api-endpoints) [[Back to README]](../../README.md)

