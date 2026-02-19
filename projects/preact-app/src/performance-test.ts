import { UserServiceClient } from './generated/rpc_client';

export interface PerformanceTestResult {
  transportMode: 'ws' | 'ble' | 'serial';
  totalRequests: number;
  successfulRequests: number;
  failedRequests: number;
  duration: number; // 毫秒
  requestsPerSecond: number;
  avgLatency: number; // 毫秒
  minLatency: number; // 毫秒
  maxLatency: number; // 毫秒
  p95Latency: number; // 毫秒
  p99Latency: number; // 毫秒
  errors: string[];
}

export interface PerformanceTestProgress {
  currentRequest: number;
  totalRequests: number;
  successfulRequests: number;
  failedRequests: number;
  elapsed: number; // 毫秒
  currentRequestsPerSecond: number;
}

export type PerformanceTestCallback = (progress: PerformanceTestProgress) => void;

/**
 * 性能测试配置
 */
export interface PerformanceTestOptions {
  /** 测试时长（秒），默认 10 秒 */
  duration?: number;
  /** 并发请求数，默认 1 */
  concurrency?: number;
  /** 测试的用户 ID，默认 1 */
  userId?: number;
  /** 请求之间的延迟（毫秒），默认 0 */
  delay?: number;
  /** 进度回调 */
  onProgress?: PerformanceTestCallback;
  /** 停止信号 */
  signal?: AbortSignal;
}

/**
 * 执行性能测试
 * @param client RPC 客户端
 * @param transportMode 传输模式
 * @param options 测试选项
 * @returns 测试结果
 */
export async function runPerformanceTest(
  client: UserServiceClient,
  transportMode: 'ws' | 'ble' | 'serial',
  options: PerformanceTestOptions = {}
): Promise<PerformanceTestResult> {
  const {
    duration = 10,
    concurrency = 1,
    userId = 1,
    delay = 0,
    onProgress,
    signal,
  } = options;

  const startTime = Date.now();
  const endTime = startTime + duration * 1000;

  let totalRequests = 0;
  let successfulRequests = 0;
  let failedRequests = 0;
  const latencies: number[] = [];
  const errors: string[] = [];

  let lastProgressTime = startTime;
  let lastProgressRequests = 0;

  // 并发控制
  const activeRequests = new Set<Promise<void>>();

  const sleep = (ms: number) => new Promise(resolve => setTimeout(resolve, ms));

  const makeRequest = async (): Promise<void> => {
    if (signal?.aborted) return;

    const requestStart = Date.now();

    try {
      // 使用 Promise.race 实现请求超时
      await Promise.race([
        client.GetUser(userId),
        new Promise<never>((_, reject) =>
          setTimeout(() => reject(new Error('Request timeout')), 5000)
        ),
      ]);

      const requestEnd = Date.now();
      const latency = requestEnd - requestStart;
      latencies.push(latency);
      successfulRequests++;
    } catch (e) {
      failedRequests++;
      const errorMsg = e instanceof Error ? e.message : String(e);
      errors.push(errorMsg);
    }

    totalRequests++;
  };

  // 主测试循环
  const runTest = async (): Promise<void> => {
    while (Date.now() < endTime && !signal?.aborted) {
      // 控制并发数
      while (activeRequests.size < concurrency && !signal?.aborted) {
        const requestPromise = makeRequest().then(() => {
          activeRequests.delete(requestPromise);
        });
        activeRequests.add(requestPromise);
      }

      // 报告进度（每 100ms 或每 100 个请求）
      const now = Date.now();
      if (onProgress && (now - lastProgressTime >= 100 || totalRequests - lastProgressRequests >= 100)) {
        const elapsed = now - startTime;
        const currentRequestsPerSecond = (totalRequests / elapsed) * 1000;
        onProgress({
          currentRequest: totalRequests,
          totalRequests: Math.floor((elapsed / 1000) * (totalRequests / (elapsed / 1000))),
          successfulRequests,
          failedRequests,
          elapsed,
          currentRequestsPerSecond,
        });
        lastProgressTime = now;
        lastProgressRequests = totalRequests;
      }

      // 请求间延迟
      if (delay > 0) {
        await sleep(delay);
      } else {
        // 避免 CPU 过载
        await sleep(1);
      }

      // 等待至少一个请求完成
      if (activeRequests.size >= concurrency) {
        await Promise.race(activeRequests);
      }
    }

    // 等待所有活跃请求完成
    await Promise.all(activeRequests);
  };

  await runTest();

  const totalDuration = Date.now() - startTime;
  const requestsPerSecond = (totalRequests / totalDuration) * 1000;

  // 计算统计数据
  if (latencies.length > 0) {
    latencies.sort((a, b) => a - b);
    const avgLatency = latencies.reduce((sum, l) => sum + l, 0) / latencies.length;
    const minLatency = latencies[0];
    const maxLatency = latencies[latencies.length - 1];
    const p95Index = Math.floor(latencies.length * 0.95);
    const p99Index = Math.floor(latencies.length * 0.99);
    const p95Latency = latencies[p95Index];
    const p99Latency = latencies[p99Index];

    return {
      transportMode,
      totalRequests,
      successfulRequests,
      failedRequests,
      duration: totalDuration,
      requestsPerSecond,
      avgLatency,
      minLatency,
      maxLatency,
      p95Latency,
      p99Latency,
      errors: errors.slice(0, 10), // 只保留前 10 个错误
    };
  }

  return {
    transportMode,
    totalRequests,
    successfulRequests,
    failedRequests,
    duration: totalDuration,
    requestsPerSecond,
    avgLatency: 0,
    minLatency: 0,
    maxLatency: 0,
    p95Latency: 0,
    p99Latency: 0,
    errors,
  };
}

/**
 * 计算理论最大请求频率
 * @param results 多个测试结果
 * @returns 每种传输模式的最大频率
 */
export function calculateMaxThroughput(
  results: PerformanceTestResult[]
): Record<'ws' | 'ble' | 'serial', number> {
  const maxThroughput: Record<string, number> = {};

  for (const result of results) {
    if (!maxThroughput[result.transportMode] ||
        result.requestsPerSecond > maxThroughput[result.transportMode]) {
      maxThroughput[result.transportMode] = result.requestsPerSecond;
    }
  }

  return {
    ws: maxThroughput.ws || 0,
    ble: maxThroughput.ble || 0,
    serial: maxThroughput.serial || 0,
  };
}

/**
 * 格式化性能测试结果为可读文本
 */
export function formatPerformanceTestResult(result: PerformanceTestResult): string {
  const lines = [
    `=== ${result.transportMode.toUpperCase()} 性能测试结果 ===`,
    `总请求数: ${result.totalRequests}`,
    `成功: ${result.successfulRequests} | 失败: ${result.failedRequests}`,
    `测试时长: ${(result.duration / 1000).toFixed(2)} 秒`,
    ``,
    `--- 吞吐量 ---`,
    `每秒请求数 (RPS): ${result.requestsPerSecond.toFixed(2)}`,
    ``,
    `--- 延迟统计 ---`,
    `平均: ${result.avgLatency.toFixed(2)} ms`,
    `最小: ${result.minLatency.toFixed(2)} ms`,
    `最大: ${result.maxLatency.toFixed(2)} ms`,
    `P95: ${result.p95Latency.toFixed(2)} ms`,
    `P99: ${result.p99Latency.toFixed(2)} ms`,
  ];

  if (result.errors.length > 0) {
    lines.push(``, `--- 错误 (前10个) ---`, ...result.errors.map(e => `- ${e}`));
  }

  return lines.join('\n');
}
