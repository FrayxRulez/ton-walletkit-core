# Ton.WalletKit.Api.Model.TONTransactionAccountState
State of an account at a specific point in time.

## Properties

Name | Type | Description | Notes
------------ | ------------- | ------------- | -------------
**Balance** | **string** | Token amount represented as a string to preserve precision. For TON, this is typically in nanotons (1 TON &#x3D; 10^9 nanotons). | 
**AccountStatus** | **TONAccountStatus** |  | [optional] 
**CodeHash** | **string** | The hash of the smart contract code | [optional] 
**DataHash** | **string** | The hash of the contract&#39;s data section | [optional] 
**ExtraCurrencies** | **Dictionary&lt;string, string&gt;** | Map of extra currency IDs to their amounts. Extra currencies are additional tokens that can be attached to TON messages. | [optional] 
**FrozenHash** | **string** | The hash of the frozen account state, if the account is frozen | [optional] 
**Hash** | **string** | The state hash of the account | [optional] 

[[Back to Model list]](../../README.md#documentation-for-models) [[Back to API list]](../../README.md#documentation-for-api-endpoints) [[Back to README]](../../README.md)

