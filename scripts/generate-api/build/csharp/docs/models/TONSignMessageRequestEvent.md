# Ton.WalletKit.Api.Model.TONSignMessageRequestEvent
Event containing a sign-message (sign-only transaction) request from a dApp via TON Connect. The wallet signs the transaction using the internal opcode and returns the signed BoC without broadcasting it on-chain.

## Properties

Name | Type | Description | Notes
------------ | ------------- | ------------- | -------------
**Id** | **string** | Unique identifier for the bridge event | 
**Preview** | [**TONSendTransactionRequestEventPreview**](TONSendTransactionRequestEventPreview.md) |  | 
**Request** | [**TONTransactionRequest**](TONTransactionRequest.md) |  | 
**DAppInfo** | [**TONDAppInfo**](TONDAppInfo.md) |  | [optional] 
**Domain** | **string** | Domain of the dApp that initiated the event | [optional] 
**From** | **string** |  | [optional] 
**IsJsBridge** | **bool** | Whether the event originated from JS Bridge (injected provider) | [optional] 
**IsLocal** | **bool** |  | [optional] 
**MessageId** | **string** |  | [optional] 
**ReturnStrategy** | **string** | Raw TonConnect return strategy string. | [optional] 
**SessionId** | **string** | Session identifier for the connection | [optional] 
**TabId** | **string** | Browser tab ID for JS Bridge events | [optional] 
**TraceId** | **string** |  | [optional] 
**WalletAddress** | **string** | User-friendly TON address representation (e.g., \&quot;EQDtFpEwcFAEcRe5mLVh2N6C0x-_hJEM7W61_JLnSF74p4q2\&quot;) | [optional] 
**WalletId** | **string** | Wallet identifier associated with the event | [optional] 

[[Back to Model list]](../../README.md#documentation-for-models) [[Back to API list]](../../README.md#documentation-for-api-endpoints) [[Back to README]](../../README.md)

