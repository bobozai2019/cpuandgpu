using System.Diagnostics;

namespace ThreadPoolCourse;

internal static class Program
{
    private sealed record Options(int Workers, int Tasks, int Iterations, string Demo);
    private static Options Parse(string[] args)
    {
        int workers = 4, tasks = 16, iterations = 20000;
        string demo = "all";
        for (int i = 0; i < args.Length; ++i)
        {
            string key = args[i];
            if (++i >= args.Length) throw new ArgumentException("Missing option value.");
            string value = args[i];
            int Number(int max) => int.TryParse(value, out int n) && n >= 1 && n <= max
                ? n : throw new ArgumentException($"Number must be 1..{max}.");
            switch (key)
            {
                case "--workers": workers = Number(64); break;
                case "--tasks": tasks = Number(10000); break;
                case "--iterations": iterations = Number(1000000); break;
                case "--demo": demo = value; break;
                default: throw new ArgumentException($"Unknown option: {key}");
            }
        }
        if (demo is not ("all" or "pool" or "counter" or "benchmark")) throw new ArgumentException("Unknown demo.");
        return new(workers, tasks, iterations, demo);
    }
    private static ulong Work(int seed, int iterations)
    {
        ulong x = (ulong)seed + 1;
        for (int i = 0; i < iterations; ++i) x = unchecked(x * 1664525UL + 1013904223UL);
        return x;
    }
    private static void PoolDemo(Options o)
    {
        using var pool = new FixedThreadPool(o.Workers);
        var tasks = new List<Task<int>>();
        for (int i = 0; i < o.Tasks; ++i)
        {
            int index = i; // 捕获每次循环独立的变量，避免共享 for 的 i。
            tasks.Add(pool.Submit(() => index * index));
        }
        long sum = tasks.Sum(t => (long)t.GetAwaiter().GetResult());
        long n = o.Tasks;
        if (sum != n * (n - 1) * (2 * n - 1) / 6) throw new Exception("Incorrect square sum.");
        Task<int> failure = pool.Submit<int>(() => throw new ApplicationException("teaching exception"));
        try { failure.GetAwaiter().GetResult(); }
        catch (ApplicationException e) { Console.WriteLine($"[pool] Task received: {e.Message}"); }
        if (pool.Submit(() => 42).GetAwaiter().GetResult() != 42) throw new Exception("Worker did not survive.");
        pool.WaitIdle();
        var s = pool.Stats;
        Console.WriteLine($"[pool] square sum={sum} completed={s.Completed} queued={s.Queued} active={s.Active}");
        pool.Shutdown();
        bool rejected = false;
        try { pool.Submit(() => 0); } catch (InvalidOperationException) { rejected = true; }
        if (!rejected) throw new Exception("Accepted after close.");
        Console.WriteLine("[pool] closed pool rejects new work: PASS");
    }
    private static void CounterDemo(Options o)
    {
        using var pool = new FixedThreadPool(o.Workers);
        object counterGate = new();
        long locked = 0, atomic = 0, expected = (long)o.Tasks * o.Iterations;
        foreach (bool useAtomic in new[] { false, true })
        {
            var results = new List<Task>();
            var watch = Stopwatch.StartNew();
            for (int t = 0; t < o.Tasks; ++t) results.Add(pool.Submit(() =>
            {
                for (int i = 0; i < o.Iterations; ++i)
                {
                    if (useAtomic) Interlocked.Increment(ref atomic);
                    else { lock (counterGate) ++locked; }
                }
            }));
            Task.WhenAll(results).GetAwaiter().GetResult();
            long count = useAtomic ? Interlocked.Read(ref atomic) : locked;
            if (count != expected) throw new Exception("Counter mismatch.");
            Console.WriteLine($"[counter] {(useAtomic ? "Interlocked" : "lock")} count={count} expected={expected} ms={watch.Elapsed.TotalMilliseconds:F3}");
        }
    }
    private static void Benchmark(Options o)
    {
        using var pool = new FixedThreadPool(o.Workers);
        pool.Submit(() => Work(0, 1000)).GetAwaiter().GetResult();
        var watch = Stopwatch.StartNew();
        ulong sequential = 0;
        for (int i = 0; i < o.Tasks; ++i) sequential ^= Work(i, o.Iterations);
        double serialMs = watch.Elapsed.TotalMilliseconds;
        watch.Restart();
        var results = new List<Task<ulong>>();
        for (int i = 0; i < o.Tasks; ++i)
        {
            int index = i;
            results.Add(pool.Submit(() => Work(index, o.Iterations)));
        }
        ulong parallel = 0;
        foreach (Task<ulong> result in results) parallel ^= result.GetAwaiter().GetResult();
        double parallelMs = watch.Elapsed.TotalMilliseconds;
        if (sequential != parallel) throw new Exception("Checksum mismatch.");
        Console.WriteLine($"[benchmark] serial_ms={serialMs:F3} pool_ms={parallelMs:F3} checksum={parallel} match=PASS");
        Console.WriteLine("Timing excludes pool construction, includes submit/get. Repeat Release runs; JIT/tiering affects results.");
    }
    public static int Main(string[] args)
    {
        try
        {
            if (args.SequenceEqual(new[] { "--test" })) { SelfTests.Run(); return 0; }
            if (args.SequenceEqual(new[] { "--help" }))
            {
                Console.WriteLine("--demo all|pool|counter|benchmark --workers 1..64 --tasks 1..10000 --iterations 1..1000000 | --test");
                return 0;
            }
            var o = Parse(args);
            Console.WriteLine($"C# / .NET 10 thread pool | workers={o.Workers} tasks={o.Tasks} iterations={o.Iterations}");
            if (o.Demo is "all" or "pool") PoolDemo(o);
            if (o.Demo is "all" or "counter") CounterDemo(o);
            if (o.Demo is "all" or "benchmark") Benchmark(o);
            return 0;
        }
        catch (Exception e) { Console.Error.WriteLine($"ERROR: {e.Message}"); return 1; }
    }
}
