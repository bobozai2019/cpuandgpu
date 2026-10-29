namespace ThreadPoolCourse;

// 教学用固定线程池。只接受同步委托；不流动 ExecutionContext，不提供异步调度器。
public sealed class FixedThreadPool : IDisposable
{
    public readonly record struct Snapshot(int Queued, int Active, long Submitted, long Completed, bool Accepting);
    private readonly object _gate = new();       // 专用于 Monitor 的 object 锁
    private readonly object _shutdownGate = new();
    private readonly Queue<Action> _queue = new();
    private readonly Thread[] _workers;
    private bool _stopping;                     // 所有读写都持有 _gate
    private int _active;
    private long _submitted;
    private long _completed;
    [ThreadStatic] private static FixedThreadPool? _currentPool;

    public FixedThreadPool(int workerCount)
    {
        if (workerCount is < 1 or > 64) throw new ArgumentOutOfRangeException(nameof(workerCount));
        _workers = new Thread[workerCount];
        int started = 0;
        try
        {
            for (int i = 0; i < workerCount; ++i)
            {
                _workers[i] = new Thread(WorkerLoop) { Name = $"course-worker-{i}", IsBackground = true };
                _workers[i].Start();
                ++started;
            }
        }
        catch
        {
            lock (_gate) { _stopping = true; Monitor.PulseAll(_gate); }
            for (int i = 0; i < started; ++i) _workers[i].Join();
            throw;
        }
    }

    public Task<T> Submit<T>(Func<T> function)
    {
        ArgumentNullException.ThrowIfNull(function);
        // 避免把 async lambda 的 Task<Task<T>> 误认为整个异步操作已完成。
        Type resultType = typeof(T);
        if (typeof(Task).IsAssignableFrom(resultType) || resultType == typeof(ValueTask) ||
            (resultType.IsGenericType && resultType.GetGenericTypeDefinition() == typeof(ValueTask<>)))
            throw new ArgumentException("Submit accepts synchronous delegates only.", nameof(function));
        var completion = new TaskCompletionSource<T>(TaskCreationOptions.RunContinuationsAsynchronously);
        lock (_gate)
        {
            if (_stopping) throw new InvalidOperationException("Pool is closed.");
            _queue.Enqueue(() =>
            {
                try { completion.SetResult(function()); }
                catch (Exception error) { completion.SetException(error); }
            });
            ++_submitted;
            // WorkerLoop 和 WaitIdle 共用 Monitor 的等待集合，必须全部通知以避免唤醒错类等待者。
            Monitor.PulseAll(_gate);
        }
        return completion.Task;
    }

    public Task Submit(Action action)
    {
        ArgumentNullException.ThrowIfNull(action);
        // async void 没有可等待的完成句柄，不纳入本池的同步工作契约。
        if (action.Method.IsDefined(typeof(System.Runtime.CompilerServices.AsyncStateMachineAttribute), false))
            throw new ArgumentException("async void is not supported.", nameof(action));
        return Submit(() => { action(); return true; });
    }

    public void WaitIdle()
    {
        RequireExternalCaller();
        lock (_gate)
            while (_queue.Count != 0 || _active != 0) Monitor.Wait(_gate);
        // 返回时曾观察到空闲；不阻止其他生产者继续入队。
    }

    public void Shutdown()
    {
        RequireExternalCaller();
        lock (_shutdownGate)
        {
            lock (_gate) { _stopping = true; Monitor.PulseAll(_gate); }
            // 不持有 _gate 等待线程结束；重复和并发调用均可。
            foreach (Thread worker in _workers) worker.Join();
        }
    }
    public void Dispose() => Shutdown();
    public long Completed => Interlocked.Read(ref _completed); // 进度，不是任务结果句柄
    public Snapshot Stats
    {
        get { lock (_gate) return new(_queue.Count, _active, _submitted, Completed, !_stopping); }
    }
    private void RequireExternalCaller()
    {
        if (_currentPool == this)
            throw new InvalidOperationException("A worker must not WaitIdle/Shutdown its own pool.");
    }
    private void WorkerLoop()
    {
        _currentPool = this;
        try
        {
            while (true)
            {
                Action job;
                lock (_gate)
                {
                    while (_queue.Count == 0 && !_stopping) Monitor.Wait(_gate);
                    if (_stopping && _queue.Count == 0) return;
                    job = _queue.Dequeue();
                    ++_active;
                }
                try { job(); } // 锁外执行，Submit 包装器将用户异常写入 Task。
                finally
                {
                    lock (_gate)
                    {
                        Interlocked.Increment(ref _completed);
                        --_active;
                        if (_queue.Count == 0 && _active == 0) Monitor.PulseAll(_gate);
                    }
                }
            }
        }
        finally { _currentPool = null; }
    }
}
