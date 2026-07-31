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

using var client = new WalletKitClient(IntPtr.Zero, IntPtr.Zero);

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

Console.WriteLine(failures == 0 ? "PASS" : "FAILED");
return failures == 0 ? 0 : 1;
