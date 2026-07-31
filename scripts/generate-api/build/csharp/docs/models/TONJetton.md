# Ton.WalletKit.Api.Model.TONJetton
Jetton fungible token on the TON blockchain (TEP-74 standard).

## Properties

Name | Type | Description | Notes
------------ | ------------- | ------------- | -------------
**Address** | **string** | User-friendly TON address representation (e.g., \&quot;EQDtFpEwcFAEcRe5mLVh2N6C0x-_hJEM7W61_JLnSF74p4q2\&quot;) | 
**Balance** | **string** | Token amount represented as a string to preserve precision. For TON, this is typically in nanotons (1 TON &#x3D; 10^9 nanotons). | 
**Info** | [**TONTokenInfo**](TONTokenInfo.md) |  | 
**IsVerified** | **bool** | Indicates if the jetton is verified | 
**Prices** | [**List&lt;TONJettonPrice&gt;**](TONJettonPrice.md) | Current prices of the jetton in various currencies | 
**WalletAddress** | **string** | User-friendly TON address representation (e.g., \&quot;EQDtFpEwcFAEcRe5mLVh2N6C0x-_hJEM7W61_JLnSF74p4q2\&quot;) | 
**DecimalsNumber** | **decimal** | The number of decimal places used by the token | [optional] 
**Extra** | **Dictionary&lt;string, Object&gt;** | Additional arbitrary data related to the jetton | [optional] 

[[Back to Model list]](../../README.md#documentation-for-models) [[Back to API list]](../../README.md#documentation-for-api-endpoints) [[Back to README]](../../README.md)

