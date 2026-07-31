# Ton.WalletKit.Api.Model.TONEmulationTransactionDescription
Detailed description of all execution phases in an emulated transaction.

## Properties

Name | Type | Description | Notes
------------ | ------------- | ------------- | -------------
**ComputePhase** | [**TONEmulationComputePhase**](TONEmulationComputePhase.md) |  | 
**IsAborted** | **bool** | Whether the transaction was aborted | 
**IsCreditFirst** | **bool** | Whether the credit phase was executed before the storage phase | 
**IsDestroyed** | **bool** | Whether the account was destroyed by this transaction | 
**IsInstalled** | **bool** | Whether a contract was installed in this transaction | 
**IsTock** | **bool** | Whether this was a tock transaction | 
**StoragePhase** | [**TONEmulationStoragePhase**](TONEmulationStoragePhase.md) |  | 
**Type** | **string** | Transaction type (e.g. \&quot;ord\&quot;, \&quot;ticktock\&quot;, \&quot;storage\&quot;) | 
**ActionPhase** | [**TONEmulationActionPhase**](TONEmulationActionPhase.md) |  | [optional] 
**CreditPhase** | [**TONEmulationCreditPhase**](TONEmulationCreditPhase.md) |  | [optional] 

[[Back to Model list]](../../README.md#documentation-for-models) [[Back to API list]](../../README.md#documentation-for-api-endpoints) [[Back to README]](../../README.md)

