const express = require("express");
const app = express();
const port = 8888;

// 允许跨域访问
app.use((req, res, next) => {
  res.setHeader("Access-Control-Allow-Origin", "*");
  next();
});

// 生成核心使用率数组
function generateCoreUsage() {
  return Array.from({ length: 22 }, () =>
    Math.random() > 0.8
      ? Number((Math.random() * 15).toFixed(2))
      : Number((Math.random() / 5).toFixed(2))
  );
}

// 生成负载统计数据
function generateLoadStats() {
  return {
    load_1: Number((Math.random() * 2).toFixed(2)),
    load_5: Number((Math.random() * 1.5).toFixed(2)),
    load_15: Number(Math.random().toFixed(2)),
    usage_percent: Number((Math.random() * 100).toFixed(1)),
  };
}

// 生成网络指标
function generateNetMetrics() {
  const base = Math.floor(Math.random() * 10000);
  return {
    packets_recv: base,
    packets_send: base,
    total_recv: base * 195,
    total_send: base * 195,
    dropin: 0,
    dropout: 0,
    errin: 0,
    errout: 0,
  };
}

// 生成请求统计数据
function generateRequestStats() {
  const generateCountArray = () =>
    Array.from({ length: 10 }, (_, i) =>
      i === 0 ? Math.floor(Math.random() * 100) : 0
    );

  const baseCount = Math.floor(Math.random() * 100);

  return {
    connection_count: Math.floor(Math.random() * 5),
    count_concurrent: 0,
    count_in_long_consumption: generateCountArray(),
    count_in_mid_consumption: generateCountArray(),
    count_in_short_consumption: generateCountArray(),
    count_last_day: { peak: 1, success: baseCount, total: baseCount },
    count_last_hour: { peak: 1, success: baseCount, total: baseCount },
    count_last_minute: { peak: 1, success: baseCount, total: baseCount },
    count_last_second: { peak: 0, success: 0, total: 0 },
    count_since_start: { peak: 1, success: baseCount, total: baseCount },
  };
}

// 生成存储节点数据
function generateStorageNode(id) {
  return {
    cpu: {
      core_usages: generateCoreUsage(),
      ...generateLoadStats(),
    },
    mem: {
      available: Math.floor(Math.random() * 16487739392),
      total: 16487739392,
    },
    net: generateNetMetrics(),
    request: generateRequestStats(),
    storage_info: {
      base_path: `/home/errlst/dfs/build/base_path/storage_${id}`,
      id: id,
      ip: "0.0.0.0",
      port: 9000 + id,
      store_infos: {
        cold: [
          {
            free_space: Math.floor(Math.random() * 1e12),
            root_path: `/home/errlst/dfs/build/base_path/storage_${id}/hot`,
            total_space: 1081101176832,
          },
        ],
        hot: [
          {
            free_space: Math.floor(Math.random() * 1e12),
            root_path: `/home/errlst/dfs/build/base_path/storage_${id}/hot`,
            total_space: 1081101176832,
          },
        ],
      },
    },
  };
}

// 主接口
app.get("/", (req, res) => {
  const responseData = {
    cpu: {
      core_usages: generateCoreUsage(),
      ...generateLoadStats(),
    },
    master_info: {
      base_path: "/home/errlst/dfs/build/base_path/master",
      ip: "0.0.0.0",
      magic: 12345678,
      port: 8888,
      storage_count: 3,
      storage_group_size: 3,
      thread_count: 3,
    },
    mem: {
      available: Math.floor(Math.random() * 16487739392),
      total: 16487739392,
    },
    net: generateNetMetrics(),
    request: generateRequestStats(),
    storage_metrics: {
      1: [generateStorageNode(1), generateStorageNode(2)],
      2: [generateStorageNode(3)],
    },
  };

  res.setHeader("Content-Type", "application/json");
  res.send(JSON.stringify(responseData, null, 2));
});

app.listen(port, () => {
  console.log(`Server running at http://localhost:${port}`);
});
