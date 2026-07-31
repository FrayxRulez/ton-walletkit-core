# Ton.WalletKit.Api.Model.TONTransactionMessage
Message sent or received in a transaction.

## Properties

Name | Type | Description | Notes
------------ | ------------- | ------------- | -------------
**CreatedAt** | **decimal** | The timestamp when the message was created | [optional] 
**CreationLogicalTime** | **string** | Logical time value used for ordering transactions on the TON blockchain | [optional] 
**Destination** | **string** | User-friendly TON address representation (e.g., \&quot;EQDtFpEwcFAEcRe5mLVh2N6C0x-_hJEM7W61_JLnSF74p4q2\&quot;) | [optional] 
**FwdFee** | **string** | Token amount represented as a string to preserve precision. For TON, this is typically in nanotons (1 TON &#x3D; 10^9 nanotons). | [optional] 
**IhrDisabled** | **bool** | IHR(Immediate hypercube routing) enabled/disabled IHR is a method of message delivery in the TON Blockchain network, where messages are sent directly to the recipient’s shardchain. | [optional] 
**IhrFee** | **string** | Token amount represented as a string to preserve precision. For TON, this is typically in nanotons (1 TON &#x3D; 10^9 nanotons). | [optional] 
**ImportFee** | **string** | Token amount represented as a string to preserve precision. For TON, this is typically in nanotons (1 TON &#x3D; 10^9 nanotons). | [optional] 
**IsBounce** | **bool** | The flag indicating if the message requested a bounce on failure | [optional] 
**IsBounced** | **bool** | The flag indicating if the message was bounced back | [optional] 
**MessageContent** | [**TONTransactionMessageContent**](TONTransactionMessageContent.md) |  | [optional] 
**Opcode** | **string** | The opcode included in the message payload | [optional] 
**Source** | **string** | User-friendly TON address representation (e.g., \&quot;EQDtFpEwcFAEcRe5mLVh2N6C0x-_hJEM7W61_JLnSF74p4q2\&quot;) | [optional] 
**Value** | **string** | Token amount represented as a string to preserve precision. For TON, this is typically in nanotons (1 TON &#x3D; 10^9 nanotons). | [optional] 
**ValueExtraCurrencies** | **Dictionary&lt;string, string&gt;** | Map of extra currency IDs to their amounts. Extra currencies are additional tokens that can be attached to TON messages. | [optional] 

[[Back to Model list]](../../README.md#documentation-for-models) [[Back to API list]](../../README.md#documentation-for-api-endpoints) [[Back to README]](../../README.md)

