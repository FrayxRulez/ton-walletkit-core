// Drives the C# binding against a real twk.dll: P/Invoke marshalling, the receive
// loop, request/response correlation, error faulting, unsolicited events — and
// the typed facade, where every payload is a generated DTO.
using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using Ton.WalletKit;
using Ton.WalletKit.Api.Model;

int failures = 0;
void Check(string name, bool ok, string detail = null)
{
    Console.WriteLine($"{(ok ? "ok" : "FAIL")}: {name}{(detail is null ? "" : " -> " + Truncate(detail))}");
    if (!ok) failures++;
}
static string Truncate(string s) => s is null ? "(null)" : (s.Length > 90 ? s.Substring(0, 90) : s);

// A test-double host: proves the delegate block, struct layout and reverse
// callbacks work without needing TDLib or PasswordVault.
var host = new FakeHost();
using var delegates = new WalletKitDelegates(host);
using var client = new WalletKitClient(delegates.Handle, IntPtr.Zero);

var events = 0;
client.EventReceived += (_, e) => Interlocked.Increment(ref events);

// 1. Round trip + correlation.
var echo = await client.SendAsync("echo", "[\"hello from C#\"]");
Check("round trip", echo != null && echo.Contains("hello from C#"), echo);

// 2. UTF-8 marshalling: default ANSI marshalling would mangle these.
const string unicode = "приветلاسلام😀";
var utf8 = await client.SendAsync("echo", "[\"" + unicode + "\"]");
Check("utf-8 survives the ABI", utf8 != null && utf8.Contains(unicode), utf8);

// 3. Errors fault the task rather than returning an envelope.
try
{
    await client.SendAsync("noSuchMethod", "[]");
    Check("error faults the task", false, "no exception thrown");
}
catch (WalletKitException ex)
{
    Check("error faults the task", ex.Message.Contains("unknown method"), ex.Message);
}

// 4. Concurrency: many in-flight requests, each answered with its own result.
var tasks = new Task<string>[50];
for (int i = 0; i < tasks.Length; i++)
{
    var n = i;
    tasks[n] = client.SendAsync("echo", "[" + n + "]");
}
var results = await Task.WhenAll(tasks);
var correlated = true;
for (int i = 0; i < results.Length; i++)
{
    if (!results[i].Contains("\"result\":" + i)) correlated = false;
}
Check($"{tasks.Length} concurrent requests each got their own result", correlated);

// 5. Unsolicited events reach the handler.
await client.SendAsync("initWalletKit", "[{\"networks\":[{\"chainId\":\"-3\"}]}]");
await client.SendAsync("emitTestEvent", "[\"connectRequest\",{\"id\":\"x\"}]");
for (int i = 0; i < 50 && Volatile.Read(ref events) == 0; i++) await Task.Delay(20);
Check("unsolicited event delivered", Volatile.Read(ref events) > 0, $"{events} event(s)");

// 6. The delegate layer: walletkit's own client must reach our HTTP host.
await client.SendAsync("initWalletKit", "[{\"networks\":[{\"chainId\":\"-3\",\"endpoint\":\"https://testnet.toncenter.com\"}]}]");
var balance = await client.SendAsync("getAddressBalance",
    "[\"-1:3333333333333333333333333333333333333333333333333333333333333333\",\"-3\"]");
Check("http delegate served walletkit", balance != null && balance.Contains("110576459116021734"), balance);
Check("host saw the request", host.Requests.Count > 0 && host.Requests[0].Contains("addressInformation"),
      host.Requests.Count > 0 ? host.Requests[0] : "(none)");

// 7. Storage delegate round trip through the core.
await client.SendAsync("storageSet", "[\"k\",\"v\"]");
var got = await client.SendAsync("storageGet", "[\"k\"]");
Check("storage delegate round trip", got != null && got.Contains("\"value\":\"v\""), got);

// 8. The typed facade: kit-ios's object model, carrying generated DTOs.
var testnet = new TONNetwork("-3");
var configuration = new TonWalletKitConfiguration
{
    NetworkConfigurations = new List<TonNetworkConfiguration>
    {
        new TonNetworkConfiguration("-3", new TonApiClientConfiguration { Url = "https://testnet.toncenter.com" }),
    },
    WalletManifest = new TonWalletManifest
    {
        Name = "InteropSmoke",
        AppName = "interop-smoke",
        ImageUrl = "https://example.test/icon.png",
        AboutUrl = "https://example.test",
        UniversalLink = "https://example.test/ton-connect",
        BridgeUrl = "https://bridge.tonapi.io/bridge",
    },
    Features = new List<TonFeature>
    {
        new TonFeature { Name = "SendTransaction", MaxMessages = 4 },
    },
    AppVersion = "0.0.1",
};

using (var kit = new TonWalletKit(new FakeHost(), configuration))
{
    await kit.InitializeAsync();
    Check("facade: initialize", kit.IsInitialized);

    var mnemonic = await kit.GenerateMnemonicAsync();
    Check("facade: mnemonic -> 24 words", mnemonic.Value.Count == 24, mnemonic.ToString());

    var signer = await kit.SignerAsync(mnemonic);
    Check("facade: signer has a public key", !string.IsNullOrEmpty(signer.PublicKey), signer.PublicKey);

    var adapter = await kit.WalletV5R1AdapterAsync(signer, new TonV5R1WalletParameters(testnet));
    Check("facade: adapter derives an address", adapter.Address?.StartsWith("UQ") == true, adapter.Address);
    Check("facade: adapter reports its network", adapter.Network?.ChainId == "-3", adapter.Network?.ChainId);

    var wallet = await kit.AddAsync(adapter);
    Check("facade: wallet registered", wallet.Id != null && wallet.Address == adapter.Address, wallet.Id);

    var walletBalance = await wallet.BalanceAsync();
    Check("facade: balance is an exact amount",
          walletBalance.ToRawString() == "110576459116021734", walletBalance.ToRawString());

    // The whole point of the task: a request DTO in, a response DTO out.
    var transfer = await wallet.TransferTonTransactionAsync(new TONTransferRequest(
        transferAmount: "1000000",
        recipientAddress: wallet.Address,
        comment: "from the facade"));
    Check("facade: transfer is a TONTransactionRequest",
          transfer is TONTransactionRequest && transfer.Messages.Count == 1,
          transfer?.Messages?[0]?.Address);
    Check("facade: transfer carries the sender", transfer.FromAddress == wallet.Address, transfer.FromAddress);

    var boc = await wallet.SignedSendTransactionAsync(transfer, new TONSignedSendTransactionOptions(fakeSignature: true));
    Check("facade: sign with a fake signature", boc != null && boc.StartsWith("te6"), boc);

    var stateInit = await wallet.StateInitAsync();
    Check("facade: state init is a BOC", stateInit != null && stateInit.StartsWith("te6"), stateInit);

    // Watch-only, no wallet involved: the state DTO comes straight from walletkit.
    var state = await kit.AddressStateAsync(wallet.Address);
    Check("facade: address state -> TONAccountState",
          state != null && state.RawBalance == "110576459116021734", state?.Status.ToString());

    // The one-step path an app actually uses on first run.
    var created = await kit.CreateWalletAsync(new TonV5R1WalletParameters(testnet));
    Check("facade: createWallet -> mnemonic + adapter",
          created.Mnemonic.Value.Count == 24 && created.WalletAdapter.Address != null,
          created.WalletAdapter.Address);

    var second = await kit.AddAsync(created.WalletAdapter);
    Check("facade: a second wallet is its own wallet", second.Id != wallet.Id, second.Address);

    // History deserializes into a DTO when walletkit's own parser accepts the
    // reply; today it usually does not (see AddressTransactionsAsync remarks).
    try
    {
        var history = await kit.AddressTransactionsAsync(wallet.Address, limit: 5);
        Check("facade: history -> TONTransactionsResponse", history != null,
              $"{history?.Transactions?.Count ?? 0} transaction(s)");
    }
    catch (WalletKitException ex)
    {
        Console.WriteLine("skip: history (upstream parser) -> " + Truncate(ex.Message));
    }

    var wallets = await kit.WalletsAsync();
    Check("facade: wallets are listed", wallets.Count == 2, $"{wallets.Count} wallet(s)");

    // Restoring the same mnemonic must yield the same address — that is what
    // makes a restored wallet the same wallet.
    var again = await kit.WalletV5R1AdapterAsync(await kit.SignerAsync(mnemonic),
                                                 new TonV5R1WalletParameters(testnet));
    Check("facade: restore is deterministic", again.Address == wallet.Address, again.Address);

    await kit.RemoveAsync(wallet.Id);
    Check("facade: remove", (await kit.WalletsAsync()).Count == 1);
}

Console.WriteLine(failures == 0 ? "PASS" : "FAILED");
return failures == 0 ? 0 : 1;

// ---- test double -----------------------------------------------------------

sealed class FakeHost : IWalletKitHost, IWalletKitHttp, IWalletKitStorage
{
    public readonly System.Collections.Generic.List<string> Requests = new();
    private readonly System.Collections.Generic.Dictionary<string, string> _store = new();

    public IWalletKitHttp Http => this;
    public IWalletKitSse Sse => null;          // TON Connect not exercised here
    public IWalletKitStorage Storage => this;
    public void Log(int level, string message) { }

    public Task<WalletKitHttpResponse> SendAsync(string method, string url, string headersJson, string body,
                                                 CancellationToken ct)
    {
        lock (Requests) Requests.Add(url);
        // A recorded toncenter reply, so this needs no network.
        return Task.FromResult(new WalletKitHttpResponse
        {
            Status = 200,
            HeadersJson = "{\"content-type\":\"application/json\"}",
            Body = "{\"balance\":\"110576459116021734\",\"status\":\"active\","
                 + "\"last_transaction_lt\":\"36612000000003\","
                 + "\"last_transaction_hash\":\"YWJjZGVmZ2hpamtsbW5vcHFyc3R1dnd4eXoxMjM0NTY3OA==\"}",
        });
    }

    public Task<string> GetAsync(string key)
    {
        lock (_store) return Task.FromResult(_store.TryGetValue(key, out var v) ? v : null);
    }
    public Task SetAsync(string key, string value)
    {
        lock (_store) _store[key] = value;
        return Task.CompletedTask;
    }
    public Task RemoveAsync(string key)
    {
        lock (_store) _store.Remove(key);
        return Task.CompletedTask;
    }
    public Task ClearAsync()
    {
        lock (_store) _store.Clear();
        return Task.CompletedTask;
    }
}
