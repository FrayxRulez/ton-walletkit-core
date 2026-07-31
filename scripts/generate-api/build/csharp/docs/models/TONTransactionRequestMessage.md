# Ton.WalletKit.Api.Model.TONTransactionRequestMessage
Individual message within a transaction request.

## Properties

Name | Type | Description | Notes
------------ | ------------- | ------------- | -------------
**Address** | **string** | Recipient wallet address in format received from caller (raw, user friendly) | 
**Amount** | **string** | Token amount represented as a string to preserve precision. For TON, this is typically in nanotons (1 TON &#x3D; 10^9 nanotons). | 
**ExtraCurrency** | **Dictionary&lt;string, string&gt;** | Map of extra currency IDs to their amounts. Extra currencies are additional tokens that can be attached to TON messages. | [optional] 
**Mode** | [**TONSendMode**](TONSendMode.md) |  | [optional] 
**Payload** | **string** | Base64-encoded string representation | [optional] 
**StateInit** | **string** | Base64-encoded string representation | [optional] 

[[Back to Model list]](../../README.md#documentation-for-models) [[Back to API list]](../../README.md#documentation-for-api-endpoints) [[Back to README]](../../README.md)

