# Ton.WalletKit.Api.Model.TONNFT
Non-fungible token (NFT) on the TON blockchain.

## Properties

Name | Type | Description | Notes
------------ | ------------- | ------------- | -------------
**Address** | **string** | User-friendly TON address representation (e.g., \&quot;EQDtFpEwcFAEcRe5mLVh2N6C0x-_hJEM7W61_JLnSF74p4q2\&quot;) | 
**Attributes** | [**List&lt;TONNFTAttribute&gt;**](TONNFTAttribute.md) | Custom attributes/traits of the NFT (e.g., rarity, properties) | [optional] 
**AuctionContractAddress** | **string** | User-friendly TON address representation (e.g., \&quot;EQDtFpEwcFAEcRe5mLVh2N6C0x-_hJEM7W61_JLnSF74p4q2\&quot;) | [optional] 
**Collection** | [**TONNFTCollection**](TONNFTCollection.md) |  | [optional] 
**Extra** | **Dictionary&lt;string, Object&gt;** | Off-chain metadata of the NFT (key-value pairs) | [optional] 
**Index** | **string** | Index of the item within its collection | [optional] 
**Info** | [**TONTokenInfo**](TONTokenInfo.md) |  | [optional] 
**IsInited** | **bool** | Whether the NFT contract has been initialized | [optional] 
**IsOnSale** | **bool** | Whether the NFT is currently listed for sale | [optional] 
**IsSoulbound** | **bool** | Whether the NFT is soulbound (non-transferable) | [optional] 
**OwnerAddress** | **string** | User-friendly TON address representation (e.g., \&quot;EQDtFpEwcFAEcRe5mLVh2N6C0x-_hJEM7W61_JLnSF74p4q2\&quot;) | [optional] 
**RealOwnerAddress** | **string** | User-friendly TON address representation (e.g., \&quot;EQDtFpEwcFAEcRe5mLVh2N6C0x-_hJEM7W61_JLnSF74p4q2\&quot;) | [optional] 
**SaleContractAddress** | **string** | User-friendly TON address representation (e.g., \&quot;EQDtFpEwcFAEcRe5mLVh2N6C0x-_hJEM7W61_JLnSF74p4q2\&quot;) | [optional] 

[[Back to Model list]](../../README.md#documentation-for-models) [[Back to API list]](../../README.md#documentation-for-api-endpoints) [[Back to README]](../../README.md)

