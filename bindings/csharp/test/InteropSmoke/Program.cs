// Drives the C# binding against a real twk.dll: P/Invoke marshalling, the receive
// loop, request/response correlation, error faulting, and unsolicited events.
using System;
using System.Threading;
using System.Threading.Tasks;
using Ton.WalletKit;

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
