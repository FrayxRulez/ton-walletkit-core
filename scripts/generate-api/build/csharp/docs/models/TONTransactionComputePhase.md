# Ton.WalletKit.Api.Model.TONTransactionComputePhase
Compute phase of transaction execution (TVM execution).

## Properties

Name | Type | Description | Notes
------------ | ------------- | ------------- | -------------
**ExitCode** | **decimal** | The exit code returned from the VM | [optional] 
**GasCredit** | **string** | Token amount represented as a string to preserve precision. For TON, this is typically in nanotons (1 TON &#x3D; 10^9 nanotons). | [optional] 
**GasFees** | **string** | Token amount represented as a string to preserve precision. For TON, this is typically in nanotons (1 TON &#x3D; 10^9 nanotons). | [optional] 
**GasLimit** | **string** | Token amount represented as a string to preserve precision. For TON, this is typically in nanotons (1 TON &#x3D; 10^9 nanotons). | [optional] 
**GasUsed** | **string** | Token amount represented as a string to preserve precision. For TON, this is typically in nanotons (1 TON &#x3D; 10^9 nanotons). | [optional] 
**IsAccountActivated** | **bool** | The flag indicating if the account was activated during compute | [optional] 
**IsMessageStateUsed** | **bool** | The flag indicating if message state was used | [optional] 
**IsSkipped** | **bool** | The flag indicating if the compute phase was skipped | [optional] 
**IsSuccess** | **bool** | The success state of the compute phase | [optional] 
**Mode** | **decimal** | The compute execution mode | [optional] 
**VmStepsNumber** | **decimal** | The number of steps executed by the VM | [optional] 

[[Back to Model list]](../../README.md#documentation-for-models) [[Back to API list]](../../README.md#documentation-for-api-endpoints) [[Back to README]](../../README.md)

