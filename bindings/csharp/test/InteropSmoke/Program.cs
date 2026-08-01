//
// Copyright (c) Fela Ameghino 2026
//
// Distributed under the MIT License. (See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT)
//
// Drives the C# binding against a real twk.dll: P/Invoke marshalling, the receive
// loop, request/response correlation, error faulting, unsolicited events — and
// the typed facade, where every payload is a generated DTO.
using System;
using System.Collections.Generic;
using System.IO;
using System.Text.Json;
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

// The values every binding must reproduce (test/conformance/golden.json), so the
// numbers below are shared rather than this harness's own opinion.
var golden = Golden.Load();

// A test-double host: proves the delegate block, struct layout and reverse
// callbacks work without needing TDLib or PasswordVault.
var host = new FakeHost(golden);
using var delegates = new WalletKitDelegates(host);
using var client = new WalletKitClient(delegates.Handle, IntPtr.Zero);

var events = 0;
client.EventReceived += (_, e) => Interlocked.Increment(ref events);

// 1. Round trip + correlation.
var echo = await client.SendAsync("echo", "[\"hello from C#\"]");
Check("round trip", echo != null && echo.Contains("hello from C#"), echo);

// 2. UTF-8 marshalling: default ANSI marshalling would mangle these.
string unicode = golden.String("expected", "unicodeEcho");
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
    Check("error faults the task",
          ex.Message.Contains(golden.String("expected", "unknownMethodError")), ex.Message);
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
Check("http delegate served walletkit",
      balance != null && balance.Contains(golden.String("expected", "balanceNanotons")), balance);
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
    // A bridge must be configured for TON Connect, and configuring one used to
    // fail initialize outright ("JS Bridge transport is not configured").
    Bridge = new TonBridgeConfiguration
    {
        BridgeUrl = "https://bridge.tonapi.io/bridge",
        JsBridgeKey = "interop-smoke",
    },
    Features = new List<TonFeature>
    {
        new TonFeature { Name = "SendTransaction", MaxMessages = 4 },
    },
    AppVersion = "0.0.1",
};

using (var kit = new TonWalletKit(new FakeHost(golden), configuration))
{
    await kit.InitializeAsync();
    Check("facade: initialize", kit.IsInitialized);

    var generated = await kit.GenerateMnemonicAsync();
    // The count, not the words: a generated mnemonic is a live key even in a test.
    Check("facade: mnemonic -> 24 words", generated.Value.Count == 24, $"{generated.Value.Count} words");

    // The golden mnemonic, so the address below is a fixed value every binding
    // must agree on rather than whatever this run happened to generate.
    var mnemonic = new TonMnemonic(golden.Strings("mnemonic"));
    var signer = await kit.SignerAsync(mnemonic);
    Check("facade: signer derives the golden public key",
          signer.PublicKey == golden.String("derived", "publicKey"), signer.PublicKey);

    var adapter = await kit.WalletV5R1AdapterAsync(signer, new TonV5R1WalletParameters(testnet));
    Check("facade: adapter derives the golden address",
          adapter.Address == golden.String("derived", "address"), adapter.Address);
    Check("facade: adapter reports its network", adapter.Network?.ChainId == "-3", adapter.Network?.ChainId);

    var wallet = await kit.AddAsync(adapter);
    Check("facade: wallet registered under the golden id",
          wallet.Id == golden.String("derived", "walletId") && wallet.Address == adapter.Address, wallet.Id);

    var walletBalance = await wallet.BalanceAsync();
    Check("facade: balance is an exact amount",
          walletBalance.ToRawString() == golden.String("expected", "balanceNanotons"), walletBalance.ToRawString());

    // The whole point of the task: a request DTO in, a response DTO out.
    var transfer = await wallet.TransferTonTransactionAsync(new TONTransferRequest(
        transferAmount: golden.String("expected", "transfer", "transferAmount"),
        recipientAddress: wallet.Address,
        comment: "from the facade"));
    Check("facade: transfer is a TONTransactionRequest",
          transfer is TONTransactionRequest && transfer.Messages.Count == 1,
          transfer?.Messages?[0]?.Address);
    Check("facade: transfer carries the sender", transfer.FromAddress == wallet.Address, transfer.FromAddress);

    var boc = await wallet.SignedSendTransactionAsync(transfer, new TONSignedSendTransactionOptions(fakeSignature: true));
    Check("facade: sign with a fake signature",
          boc != null && boc.StartsWith(golden.String("expected", "transfer", "signedBocPrefix")), boc);

    var stateInit = await wallet.StateInitAsync();
    Check("facade: state init is a BOC", stateInit != null && stateInit.StartsWith("te6"), stateInit);

    // Watch-only, no wallet involved: the state DTO comes straight from walletkit.
    var state = await kit.AddressStateAsync(wallet.Address);
    Check("facade: address state -> TONAccountState",
          state != null && state.RawBalance == golden.String("expected", "balanceNanotons"),
          state?.Status.ToString());

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
    private readonly Golden _golden;

    public FakeHost(Golden golden) => _golden = golden;

    public IWalletKitHttp Http => this;
    public IWalletKitSse Sse => null;          // TON Connect not exercised here
    public IWalletKitStorage Storage => this;
    public void Log(int level, string message) { }

    public Task<WalletKitHttpResponse> SendAsync(string method, string url, string headersJson, string body,
                                                 CancellationToken ct)
    {
        lock (Requests) Requests.Add(url);
        // The recorded toncenter reply from the golden file, so this needs no
        // network and every binding sees the same chain state.
        return Task.FromResult(new WalletKitHttpResponse
        {
            Status = 200,
            HeadersJson = "{\"content-type\":\"application/json\"}",
            Body = _golden.String("httpFixture", "body"),
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

/// <summary>The shared expectations from test/conformance/golden.json.</summary>
sealed class Golden
{
    private readonly JsonDocument _document;

    private Golden(JsonDocument document) => _document = document;

    public static Golden Load()
    {
        // Walk up to the repository root: the harness runs from bin/Debug/…
        var directory = new DirectoryInfo(AppContext.BaseDirectory);
        while (directory != null &&
               !File.Exists(Path.Combine(directory.FullName, "test", "conformance", "golden.json")))
        {
            directory = directory.Parent;
        }
        if (directory == null)
        {
            throw new FileNotFoundException("test/conformance/golden.json not found above " + AppContext.BaseDirectory);
        }
        return new Golden(JsonDocument.Parse(File.ReadAllText(
            Path.Combine(directory.FullName, "test", "conformance", "golden.json"))));
    }

    /// <summary>A string at a path, e.g. String("derived", "address").</summary>
    public string String(params string[] path)
    {
        var element = _document.RootElement;
        foreach (var step in path)
        {
            element = element.GetProperty(step);
        }
        return element.GetString();
    }

    /// <summary>A string array member.</summary>
    public IReadOnlyList<string> Strings(string name)
    {
        var values = new List<string>();
        foreach (var element in _document.RootElement.GetProperty(name).EnumerateArray())
        {
            values.Add(element.GetString());
        }
        return values;
    }
}
