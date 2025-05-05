export const config = {
  // API配置
  api: {
    baseURL: 'http://localhost:8888'
  },
  
  // 数据更新间隔（毫秒）
  updateInterval: 5000,
  
  // 性能指标阈值配置
  thresholds: {
    cpu: {
      warning: 70,
      danger: 90
    },
    memory: {
      warning: 75,
      danger: 90
    },
    disk: {
      warning: 80,
      danger: 95
    },
    network: {
      warning: 70,
      danger: 90
    }
  }
} 