namespace ThreadPoolCourse;

internal static class SelfTests
{
    private static void Check(bool ok, string message) { if (!ok) throw new Exception(message); }
    private static void Throws<T>(Action action) where T : Exception
    {
        bool caught = false;
        try { action(); } catch (T) { caught = true; }
        Check(caught, $"Expected {typeof(T).Name}");
    }
    private static void ValuesAndExceptions()
    {
        using var pool = new FixedThreadPool(1);
        Check(pool.Submit(() => 42).GetAwaiter().GetResult() == 42, "Return value");
        int written = 0;
        pool.Submit(() => { written = 9; }).GetAwaiter().GetResult();
        Check(written == 9, "Void result visibility");
        var bad = pool.Submit<int>(() => throw new ApplicationException("expected"));
        Throws<ApplicationException>(() => bad.GetAwaiter().GetResult());
        Check(pool.Submit(() => 5).GetAwaiter().GetResult() == 5, "Worker survives exception");
        pool.WaitIdle();
        Check(pool.Completed == 4, "Faulted task counted as completed");
    }
    private static void ConcurrentProducers()
    {
        using var pool = new FixedThreadPool(4);
        int[] hits = new int[400];
        Task[] producers = Enumerable.Range(0, 4).Select(p => Task.Run(() =>
        {
            Task[] jobs = Enumerable.Range(0, 100)
                .Select(i => pool.Submit(() => { Interlocked.Increment(ref hits[p * 100 + i]); })).ToArray();
            Task.WhenAll(jobs).GetAwaiter().GetResult();
        })).ToArray();
        Task.WhenAll(producers).GetAwaiter().GetResult();
        pool.WaitIdle();
        Check(hits.All(n => n == 1), "Each job exactly once");
        var s = pool.Stats;
        Check(s.Submitted == 400 && s.Completed == 400 && s.Active == 0 && s.Queued == 0, "Stats invariant");
    }
    private static void BoundedParallelismAndIdle()
    {
        using var pool = new FixedThreadPool(3);
        using var release = new ManualResetEventSlim(false);
        using var started = new CountdownEvent(3);
        int begun = 0;
        Task[] jobs = Enumerable.Range(0, 12).Select(_ => pool.Submit(() =>
        {
            if (Interlocked.Increment(ref begun) <= 3) started.Signal();
            release.Wait();
        })).ToArray();
        bool allStarted = started.Wait(TimeSpan.FromSeconds(5));
        var s = pool.Stats;
        using var waiting = new ManualResetEventSlim(false);
        var idle = Task.Run(() => { waiting.Set(); pool.WaitIdle(); });
        waiting.Wait();
        bool blocked = !idle.Wait(30);
        release.Set(); // 先释放任务再断言，保证失败时仍能 Dispose。
        Task.WhenAll(jobs).GetAwaiter().GetResult();
        idle.GetAwaiter().GetResult();
        Check(allStarted && s.Active == 3 && s.Queued == 9, "Fixed worker count");
        Check(blocked && pool.Completed == 12, "Idle waits for completion");
    }
    private static void EmptyQueueIsNotIdle()
    {
        using var pool = new FixedThreadPool(1);
        using var release = new ManualResetEventSlim(false);
        using var started = new ManualResetEventSlim(false);
        var job = pool.Submit(() => { started.Set(); release.Wait(); });
        started.Wait();
        var s = pool.Stats;
        var idle = Task.Run(pool.WaitIdle);
        bool blocked = !idle.Wait(30);
        release.Set();
        job.GetAwaiter().GetResult(); idle.GetAwaiter().GetResult();
        Check(s.Queued == 0 && s.Active == 1 && blocked, "Empty queue is not idle");
    }
    private static void ShutdownRacesAndDrains()
    {
        using var pool = new FixedThreadPool(4);
        int accepted = 0, executed = 0;
        for (int i = 0; i < 20; ++i) { pool.Submit(() => Interlocked.Increment(ref executed)); ++accepted; }
        using var release = new ManualResetEventSlim(false);
        var producer = Task.Run(() =>
        {
            release.Wait();
            for (int i = 0; i < 300; ++i)
            {
                try { pool.Submit(() => Interlocked.Increment(ref executed)); Interlocked.Increment(ref accepted); }
                catch (InvalidOperationException) { break; }
            }
        });
        var closer1 = Task.Run(() => { release.Wait(); pool.Shutdown(); });
        var closer2 = Task.Run(() => { release.Wait(); pool.Shutdown(); });
        release.Set();
        Task.WhenAll(producer, closer1, closer2).GetAwaiter().GetResult();
        pool.Shutdown();
        Throws<InvalidOperationException>(() => pool.Submit(() => 0));
        Check(accepted == executed && pool.Completed == accepted && !pool.Stats.Accepting, "Drain and reject");
    }
    private static void OwnWorkerGuards()
    {
        using var pool = new FixedThreadPool(1);
        Throws<InvalidOperationException>(() => pool.Submit(pool.WaitIdle).GetAwaiter().GetResult());
        Throws<InvalidOperationException>(() => pool.Submit(pool.Shutdown).GetAwaiter().GetResult());
        Check(pool.Submit(() => 1).GetAwaiter().GetResult() == 1, "Guard preserves worker");
    }
    private static void LifecycleAndSyncContract()
    {
        Throws<ArgumentOutOfRangeException>(() => new FixedThreadPool(0));
        Throws<ArgumentOutOfRangeException>(() => new FixedThreadPool(65));
        using (var empty = new FixedThreadPool(2)) { empty.WaitIdle(); empty.Shutdown(); empty.Shutdown(); }
        int count = 0;
        using (var pool = new FixedThreadPool(2))
        {
            Throws<ArgumentNullException>(() => pool.Submit((Action)null!));
            Throws<ArgumentException>(() => pool.Submit(() => Task.FromResult(1)));
            Throws<ArgumentException>(() => pool.Submit(() => ValueTask.FromResult(1)));
            Action asyncVoid = async () => { await Task.Yield(); };
            Throws<ArgumentException>(() => pool.Submit(asyncVoid));
            for (int i = 0; i < 80; ++i) pool.Submit(() => Interlocked.Increment(ref count));
        }
        Check(count == 80, "Dispose drains work");
    }
    public static void Run()
    {
        (string Name, Action Run)[] tests = [
            ("values/exceptions", ValuesAndExceptions), ("concurrent producers/exactly once", ConcurrentProducers),
            ("bounded parallelism/wait idle", BoundedParallelismAndIdle), ("empty queue vs active task", EmptyQueueIsNotIdle),
            ("concurrent shutdown/drain/reject", ShutdownRacesAndDrains), ("own-worker guards", OwnWorkerGuards),
            ("validation/dispose/sync contract", LifecycleAndSyncContract)
        ];
        foreach (var test in tests) { test.Run(); Console.WriteLine($"PASS {test.Name}"); }
        Console.WriteLine("7/7 test groups passed");
    }
}
