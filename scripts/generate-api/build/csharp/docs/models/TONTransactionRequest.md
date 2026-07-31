# Ton.WalletKit.Api.Model.TONTransactionRequest
Request to send a transaction on the TON blockchain. Contains `messages` or `items`. If items are present, but messages are not — wallet app is responsible for resolving items into messages.

## Properties

Name | Type | Description | Notes
------------ | ------------- | ------------- | -------------
**Messages** | [**List&lt;TONTransactionRequestMessage&gt;**](TONTransactionRequestMessage.md) | List of messages to include in the transaction | 
**FromAddress** | **string** | Sender wallet address in received format(raw, user friendly) | [optional] 
**Items** | [**List&lt;TONStructuredItem&gt;**](TONStructuredItem.md) | List of structured items (ton/jetton/nft) as an alternative to raw messages. When present, the wallet app is responsible for resolving items into messages. | [optional] 
**Network** | [**TONNetwork**](TONNetwork.md) |  | [optional] 
**ValidUntil** | **decimal** | Unix timestamp after which the transaction becomes invalid | [optional] 

[[Back to Model list]](../../README.md#documentation-for-models) [[Back to API list]](../../README.md#documentation-for-api-endpoints) [[Back to README]](../../README.md)

