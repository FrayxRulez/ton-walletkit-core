# Ton.WalletKit.Api.Model.TONEmulationMessage
Message sent or received within an emulated transaction trace.

## Properties

Name | Type | Description | Notes
------------ | ------------- | ------------- | -------------
**Destination** | **string** | User-friendly TON address representation (e.g., \&quot;EQDtFpEwcFAEcRe5mLVh2N6C0x-_hJEM7W61_JLnSF74p4q2\&quot;) | 
**MessageContent** | [**TONEmulationMessageContent**](TONEmulationMessageContent.md) |  | 
**ValueExtraCurrencies** | **Dictionary&lt;string, string&gt;** | Map of extra currency IDs to their amounts. Extra currencies are additional tokens that can be attached to TON messages. | 
**CreatedAt** | **decimal** | Unix timestamp when the message was created, or undefined for external inbound messages | [optional] 
**CreatedLt** | **string** | Logical time value used for ordering transactions on the TON blockchain | [optional] 
**FwdFee** | **string** | Token amount represented as a string to preserve precision. For TON, this is typically in nanotons (1 TON &#x3D; 10^9 nanotons). | [optional] 
**IhrDisabled** | **bool** | Whether IHR delivery is disabled, or undefined for external inbound messages | [optional] 
**IhrFee** | **string** | Token amount represented as a string to preserve precision. For TON, this is typically in nanotons (1 TON &#x3D; 10^9 nanotons). | [optional] 
**ImportFee** | **string** | Token amount represented as a string to preserve precision. For TON, this is typically in nanotons (1 TON &#x3D; 10^9 nanotons). | [optional] 
**InitState** | **Object** | Initial state (StateInit) attached to the message, if any | [optional] 
**IsBounce** | **bool** | Whether the message requested a bounce on failure, or undefined for external inbound messages | [optional] 
**IsBounced** | **bool** | Whether the message was bounced back, or undefined for external inbound messages | [optional] 
**Source** | **string** | User-friendly TON address representation (e.g., \&quot;EQDtFpEwcFAEcRe5mLVh2N6C0x-_hJEM7W61_JLnSF74p4q2\&quot;) | [optional] 
**Value** | **string** | Token amount represented as a string to preserve precision. For TON, this is typically in nanotons (1 TON &#x3D; 10^9 nanotons). | [optional] 

[[Back to Model list]](../../README.md#documentation-for-models) [[Back to API list]](../../README.md#documentation-for-api-endpoints) [[Back to README]](../../README.md)

