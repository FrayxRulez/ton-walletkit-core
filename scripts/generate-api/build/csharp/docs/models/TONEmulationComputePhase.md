# Ton.WalletKit.Api.Model.TONEmulationComputePhase
Compute phase of transaction execution (TVM execution).

## Properties

Name | Type | Description | Notes
------------ | ------------- | ------------- | -------------
**ExitCode** | **decimal** | TVM exit code | 
**GasFees** | **string** | Token amount represented as a string to preserve precision. For TON, this is typically in nanotons (1 TON &#x3D; 10^9 nanotons). | 
**GasLimit** | **string** | Gas limit for this execution | 
**GasUsed** | **string** | Total gas consumed | 
**IsAccountActivated** | **bool** | Whether the account was activated during compute | 
**IsMsgStateUsed** | **bool** | Whether the message state was used during compute | 
**IsSkipped** | **bool** | Whether the compute phase was skipped | 
**IsSuccess** | **bool** | Whether the TVM execution succeeded | 
**Mode** | **decimal** | Compute execution mode | 
**VmSteps** | **decimal** | Number of TVM steps executed | 
**GasCredit** | **string** | Gas credit, if any | [optional] 

[[Back to Model list]](../../README.md#documentation-for-models) [[Back to API list]](../../README.md#documentation-for-api-endpoints) [[Back to README]](../../README.md)

