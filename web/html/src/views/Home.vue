<script setup>
import { usePerformanceStore } from '../stores/performance'
import { onMounted, onUnmounted } from 'vue'
import { useRouter } from 'vue-router'
import { config } from '../config/config'

const store = usePerformanceStore()
const router = useRouter()
let updateTimer = null

const fetchData = async () => {
  try {
    await store.fetchMasterNodeData()
    console.log('获取到的数据:', store.masterNode)
    if (store.error) {
      console.error('数据获取错误:', store.error)
    }
  } catch (error) {
    console.error('数据获取失败:', error)
  }
}

onMounted(async () => {
  await fetchData()
  updateTimer = setInterval(fetchData, config.updateInterval)
  console.log('组件挂载完成，定时器已启动')
})

onUnmounted(() => {
  if (updateTimer) {
    clearInterval(updateTimer)
    console.log('组件卸载，定时器已清除')
  }
})

const goToNodes = () => {
  router.push('/nodes')
}

const getStatusClass = (metric, value) => {
  return `status-${store.getMetricStatus(metric, value)}`
}

const formatBytes = (bytes) => {
  if (bytes === 0) return '0 B'
  const k = 1024
  const sizes = ['B', 'KB', 'MB', 'GB', 'TB']
  const i = Math.floor(Math.log(bytes) / Math.log(k))
  return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i]
}

const calculateUsagePercent = (total, free) => {
  if (total === 0) return 0
  return Math.round(((total - free) / total) * 100)
}

const getNodesStatus = () => {
  if (!store.groups || store.groups.length === 0) return 'normal'

  // 检查所有节点的所有指标
  for (const group of store.groups) {
    for (const node of group.nodes) {
      const metrics = [
        { type: 'cpu', value: node.cpu },
        { type: 'memory', value: node.memory },
        { type: 'network', value: node.network },
        { type: 'storage', value: calculateUsagePercent(node.hotStorage.total, node.hotStorage.free) },
        { type: 'storage', value: calculateUsagePercent(node.coldStorage.total, node.coldStorage.free) }
      ]

      // 如果任何一个节点的任何指标处于危险状态，整体就是危险状态
      if (metrics.some(m => store.getMetricStatus(m.type, m.value) === 'danger')) {
        return 'danger'
      }
    }
  }

  // 检查警告状态
  for (const group of store.groups) {
    for (const node of group.nodes) {
      const metrics = [
        { type: 'cpu', value: node.cpu },
        { type: 'memory', value: node.memory },
        { type: 'network', value: node.network },
        { type: 'storage', value: calculateUsagePercent(node.hotStorage.total, node.hotStorage.free) },
        { type: 'storage', value: calculateUsagePercent(node.coldStorage.total, node.coldStorage.free) }
      ]

      // 如果有节点处于警告状态，且没有节点处于危险状态
      if (metrics.some(m => store.getMetricStatus(m.type, m.value) === 'warning')) {
        return 'warning'
      }
    }
  }

  return 'normal'
}
</script>

<template>
  <div class="home-container">
    <div class="header-container">
      <h1>主节点性能监控</h1>
      <div v-if="store.lastUpdate" class="last-update">
        最后更新: {{ store.lastUpdate.toLocaleTimeString() }}
      </div>
    </div>

    <div v-if="store.error" class="error-message">
      获取数据失败: {{ store.error }}
    </div>
    <div v-else class="metrics-container">
      <!-- CPU信息卡片 -->
      <div class="metric-card" :class="getStatusClass('cpu', store.masterNode.cpu)">
        <h3>CPU信息</h3>
        <div class="metric-value">{{ store.masterNode.cpu }}%</div>
        <div class="metric-details">
          <div class="detail-item">
            <span class="detail-label">核心数:</span>
            <span class="detail-value">{{ store.masterNode.cpuCores }}</span>
          </div>
          <div class="detail-item">
            <span class="detail-label">1分钟负载:</span>
            <span class="detail-value">{{ store.masterNode.load1 }}</span>
          </div>
          <div class="detail-item">
            <span class="detail-label">5分钟负载:</span>
            <span class="detail-value">{{ store.masterNode.load5 }}</span>
          </div>
          <div class="detail-item">
            <span class="detail-label">15分钟负载:</span>
            <span class="detail-value">{{ store.masterNode.load15 }}</span>
          </div>
        </div>
      </div>
      
      <!-- 内存信息卡片 -->
      <div class="metric-card" :class="getStatusClass('memory', store.masterNode.memory)">
        <h3>内存信息</h3>
        <div class="metric-value">{{ store.masterNode.memory }}%</div>
        <div class="metric-details">
          <div class="detail-item">
            <span class="detail-label">总内存:</span>
            <span class="detail-value">{{ formatBytes(store.masterNode.totalMemory) }}</span>
          </div>
          <div class="detail-item">
            <span class="detail-label">可用内存:</span>
            <span class="detail-value">{{ formatBytes(store.masterNode.availableMemory) }}</span>
          </div>
        </div>
      </div>
      
      <!-- 网络信息卡片 -->
      <div class="metric-card" :class="getStatusClass('network', store.masterNode.network)">
        <h3>网络信息</h3>
        <div class="metric-value">{{ store.masterNode.network }} MB/s</div>
        <div class="metric-details">
          <div class="detail-item">
            <span class="detail-label">接收数据包:</span>
            <span class="detail-value">{{ store.masterNode.packetsRecv }}</span>
          </div>
          <div class="detail-item">
            <span class="detail-label">发送数据包:</span>
            <span class="detail-value">{{ store.masterNode.packetsSend }}</span>
          </div>
          <div class="detail-item">
            <span class="detail-label">接收错误:</span>
            <span class="detail-value">{{ store.masterNode.errin }}</span>
          </div>
          <div class="detail-item">
            <span class="detail-label">发送错误:</span>
            <span class="detail-value">{{ store.masterNode.errout }}</span>
          </div>
          <div class="detail-item">
            <span class="detail-label">丢包(入):</span>
            <span class="detail-value">{{ store.masterNode.dropin }}</span>
          </div>
          <div class="detail-item">
            <span class="detail-label">丢包(出):</span>
            <span class="detail-value">{{ store.masterNode.dropout }}</span>
          </div>
        </div>
      </div>
      
      <!-- 系统信息卡片 -->
      <div class="metric-card">
        <h3>系统信息</h3>
        <div class="metric-details">
          <div class="detail-item">
            <span class="detail-label">IP地址:</span>
            <span class="detail-value">{{ store.masterNode.ip }}</span>
          </div>
          <div class="detail-item">
            <span class="detail-label">端口:</span>
            <span class="detail-value">{{ store.masterNode.port }}</span>
          </div>
          <div class="detail-item">
            <span class="detail-label">基础路径:</span>
            <span class="detail-value">{{ store.masterNode.basePath }}</span>
          </div>
          <div class="detail-item">
            <span class="detail-label">存储组大小:</span>
            <span class="detail-value">{{ store.masterNode.storageGroupSize }}</span>
          </div>
          <div class="detail-item">
            <span class="detail-label">存储节点数:</span>
            <span class="detail-value">{{ store.masterNode.storageCount }}</span>
          </div>
          <div class="detail-item">
            <span class="detail-label">线程数:</span>
            <span class="detail-value">{{ store.masterNode.threadCount }}</span>
          </div>
        </div>
      </div>

      <!-- 请求统计卡片 -->
      <div class="metric-card">
        <h3>请求统计</h3>
        <div class="metric-details">
          <div class="detail-item">
            <span class="detail-label">当前连接数</span>
            <span class="detail-value">{{ store.masterNode.connectionCount }}</span>
          </div>
          <div class="detail-item">
            <span class="detail-label">并发请求数</span>
            <span class="detail-value">{{ store.masterNode.countConcurrent }}</span>
          </div>
          <div class="detail-item">
            <span class="detail-label">最近1秒请求</span>
            <span class="detail-value">{{ store.masterNode.countLastSecond?.total || 0 }}</span>
          </div>
          <div class="detail-item">
            <span class="detail-label">1秒成功数</span>
            <span class="detail-value">{{ store.masterNode.countLastSecond?.success || 0 }}</span>
          </div>
          <div class="detail-item">
            <span class="detail-label">1秒峰值</span>
            <span class="detail-value">{{ store.masterNode.countLastSecond?.peak || 0 }}</span>
          </div>
          <div class="detail-item">
            <span class="detail-label">最近1分钟</span>
            <span class="detail-value">{{ store.masterNode.countLastMinute?.total || 0 }}</span>
          </div>
          <div class="detail-item">
            <span class="detail-label">1分钟成功数</span>
            <span class="detail-value">{{ store.masterNode.countLastMinute?.success || 0 }}</span>
          </div>
          <div class="detail-item">
            <span class="detail-label">1分钟峰值</span>
            <span class="detail-value">{{ store.masterNode.countLastMinute?.peak || 0 }}</span>
          </div>
          <div class="detail-item">
            <span class="detail-label">最近1小时</span>
            <span class="detail-value">{{ store.masterNode.countLastHour?.total || 0 }}</span>
          </div>
          <div class="detail-item">
            <span class="detail-label">1小时成功数</span>
            <span class="detail-value">{{ store.masterNode.countLastHour?.success || 0 }}</span>
          </div>
          <div class="detail-item">
            <span class="detail-label">1小时峰值</span>
            <span class="detail-value">{{ store.masterNode.countLastHour?.peak || 0 }}</span>
          </div>
          <div class="detail-item">
            <span class="detail-label">最近1天</span>
            <span class="detail-value">{{ store.masterNode.countLastDay?.total || 0 }}</span>
          </div>
          <div class="detail-item">
            <span class="detail-label">1天成功数</span>
            <span class="detail-value">{{ store.masterNode.countLastDay?.success || 0 }}</span>
          </div>
          <div class="detail-item">
            <span class="detail-label">1天峰值</span>
            <span class="detail-value">{{ store.masterNode.countLastDay?.peak || 0 }}</span>
          </div>
        </div>
      </div>

      <!-- 查看子节点卡片 -->
      <div class="metric-card action-card" :class="'status-' + getNodesStatus()" @click="goToNodes">
        <h3>子节点管理</h3>
        <div class="action-content">
          <div class="action-icon">📊</div>
          <div class="action-text">
            <template v-if="getNodesStatus() === 'danger'">
              ⚠️ 存在异常节点，请立即处理！
            </template>
            <template v-else-if="getNodesStatus() === 'warning'">
              ⚠️ 部分节点需要关注
            </template>
            <template v-else>
              查看所有子节点性能数据
            </template>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.home-container {
  padding: 0;
  height: 100vh;
  width: 100vw;
  max-width: 100%;
  background: linear-gradient(135deg, #0f172a 0%, #1e293b 100%);
  display: flex;
  flex-direction: column;
  box-sizing: border-box;
  overflow: hidden;
  position: absolute;
  left: 0;
  right: 0;
  color: #e2e8f0;
}

.header-container {
  position: relative;
  width: 100%;
  background: rgba(30, 41, 59, 0.8);
  box-shadow: 0 4px 6px rgba(0, 0, 0, 0.1);
  backdrop-filter: blur(10px);
  padding: 20px 0;
  display: flex;
  justify-content: center;
  align-items: center;
}

h1 {
  margin: 0;
  color: #e2e8f0;
  font-size: 2em;
  text-transform: uppercase;
  letter-spacing: 2px;
  font-weight: 600;
  position: relative;
}

h1::after {
  content: '';
  position: absolute;
  bottom: -10px;
  left: 50%;
  transform: translateX(-50%);
  width: 100px;
  height: 3px;
  background: linear-gradient(90deg, transparent, #3b82f6, transparent);
}

.metrics-container {
  flex: 1;
  display: grid;
  grid-template-columns: repeat(4, 1fr);
  grid-template-rows: repeat(2, 1fr);
  gap: 20px;
  padding: 20px;
  box-sizing: border-box;
  overflow: hidden;
  background: transparent;
  width: 100%;
}

.metric-card {
  background: rgba(30, 41, 59, 0.6);
  padding: 20px;
  border-radius: 12px;
  box-shadow: 0 8px 32px rgba(0, 0, 0, 0.1);
  transition: all 0.3s ease;
  display: flex;
  flex-direction: column;
  overflow: hidden;
  position: relative;
  min-width: 0;
  border: 1px solid rgba(255, 255, 255, 0.1);
  backdrop-filter: blur(10px);
}

.metric-card::before {
  content: '';
  position: absolute;
  top: 0;
  left: 0;
  right: 0;
  height: 1px;
  background: linear-gradient(90deg, transparent, rgba(59, 130, 246, 0.5), transparent);
}

.metric-card:hover {
  transform: translateY(-5px);
  box-shadow: 0 12px 40px rgba(0, 0, 0, 0.2);
  border-color: rgba(59, 130, 246, 0.3);
}

.metric-card h3 {
  color: #e2e8f0;
  margin: 0 0 15px 0;
  font-size: 1.3em;
  text-align: center;
  font-weight: 600;
  text-transform: uppercase;
  letter-spacing: 1px;
}

.metric-value {
  font-size: 2.5em;
  font-weight: bold;
  text-align: center;
  margin: 15px 0;
  color: #3b82f6;
  text-shadow: 0 0 10px rgba(59, 130, 246, 0.3);
  font-family: 'Arial', sans-serif;
}

.metric-details {
  flex: 1;
  margin-top: 20px;
  padding: 20px;
  border-top: 2px solid rgba(59, 130, 246, 0.2);
  overflow: auto;
  display: grid;
  grid-template-columns: repeat(2, 1fr);
  gap: 15px;
  min-height: 0;
  position: relative;
}

.metric-details::before {
  content: '';
  position: absolute;
  top: -2px;
  left: 0;
  right: 0;
  height: 2px;
  background: linear-gradient(90deg, transparent, rgba(59, 130, 246, 0.3), transparent);
}

.detail-item {
  background: rgba(30, 41, 59, 0.8);
  padding: 15px 20px;
  border-radius: 12px;
  transition: all 0.3s ease;
  min-width: 0;
  border: 1px solid rgba(59, 130, 246, 0.1);
  box-shadow: 0 4px 6px rgba(0, 0, 0, 0.1);
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.detail-item:hover {
  background: rgba(59, 130, 246, 0.1);
  border-color: rgba(59, 130, 246, 0.3);
  transform: translateY(-2px);
  box-shadow: 0 8px 16px rgba(59, 130, 246, 0.15);
}

.detail-label {
  color: #94a3b8;
  font-size: 0.9em;
  font-weight: 500;
  letter-spacing: 0.5px;
  text-transform: uppercase;
  margin-bottom: 4px;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.detail-value {
  color: #e2e8f0;
  font-weight: 600;
  font-size: 1.2em;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
  padding: 4px 8px;
  background: rgba(30, 41, 59, 0.6);
  border-radius: 6px;
  border: 1px solid rgba(59, 130, 246, 0.2);
  text-align: center;
}

.metric-card.status-normal {
  border-left: 4px solid #10b981;
}

.metric-card.status-warning {
  border-left: 4px solid #f59e0b;
}

.metric-card.status-danger {
  border-left: 4px solid #ef4444;
  background: linear-gradient(135deg, rgba(30, 41, 59, 0.6) 0%, rgba(239, 68, 68, 0.1) 100%);
  animation: dangerPulse 2s infinite;
}

@keyframes dangerPulse {
  0% {
    box-shadow: 0 0 0 0 rgba(239, 68, 68, 0.4);
  }
  70% {
    box-shadow: 0 0 0 10px rgba(239, 68, 68, 0);
  }
  100% {
    box-shadow: 0 0 0 0 rgba(239, 68, 68, 0);
  }
}

.metric-card.status-danger .metric-value {
  color: #ef4444;
  text-shadow: 0 0 10px rgba(239, 68, 68, 0.5);
  animation: valuePulse 1.5s infinite;
}

@keyframes valuePulse {
  0% {
    transform: scale(1);
  }
  50% {
    transform: scale(1.05);
  }
  100% {
    transform: scale(1);
  }
}

.metric-card.status-danger::before {
  background: linear-gradient(90deg, transparent, rgba(239, 68, 68, 0.5), transparent);
  animation: linePulse 2s infinite;
}

@keyframes linePulse {
  0% {
    opacity: 0.5;
  }
  50% {
    opacity: 1;
  }
  100% {
    opacity: 0.5;
  }
}

.metric-card.status-danger .detail-item {
  border-color: rgba(239, 68, 68, 0.2);
}

.metric-card.status-danger .detail-item:hover {
  background: rgba(239, 68, 68, 0.1);
  border-color: rgba(239, 68, 68, 0.3);
  box-shadow: 0 8px 16px rgba(239, 68, 68, 0.15);
}

.metric-card.status-warning .detail-item {
  border-color: rgba(245, 158, 11, 0.2);
}

.metric-card.status-warning .detail-item:hover {
  background: rgba(245, 158, 11, 0.1);
  border-color: rgba(245, 158, 11, 0.3);
  box-shadow: 0 8px 16px rgba(245, 158, 11, 0.15);
}

.action-card {
  cursor: pointer;
  display: flex;
  flex-direction: column;
  justify-content: center;
  align-items: center;
  background: linear-gradient(135deg, rgba(59, 130, 246, 0.8) 0%, rgba(37, 99, 235, 0.8) 100%);
  color: white;
  border: none;
  position: relative;
  overflow: hidden;
}

.action-card:hover {
  transform: translateY(-5px);
  box-shadow: 0 12px 40px rgba(59, 130, 246, 0.3);
}

.action-card.status-danger {
  background: linear-gradient(135deg, rgba(239, 68, 68, 0.9) 0%, rgba(185, 28, 28, 0.9) 100%);
  animation: dangerPulse 2s infinite;
}

.action-card.status-warning {
  background: linear-gradient(135deg, rgba(245, 158, 11, 0.9) 0%, rgba(194, 120, 3, 0.9) 100%);
}

.action-card.status-danger .action-text {
  color: #fecaca;
  font-weight: bold;
}

.action-card.status-warning .action-text {
  color: #fef3c7;
  font-weight: bold;
}

.action-content {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  flex: 1;
  text-align: center;
}

.action-icon {
  font-size: 3.5em;
  margin-bottom: 15px;
  text-shadow: 0 0 10px rgba(255, 255, 255, 0.3);
}

.action-text {
  font-size: 1.2em;
  font-weight: bold;
  text-align: center;
  text-transform: uppercase;
  letter-spacing: 1px;
}

.error-message {
  background: rgba(239, 68, 68, 0.2);
  color: #ef4444;
  padding: 12px;
  border-radius: 8px;
  margin: 15px;
  text-align: center;
  font-weight: bold;
  box-shadow: 0 4px 6px rgba(0, 0, 0, 0.1);
  border: 1px solid rgba(239, 68, 68, 0.3);
}

.last-update {
  position: absolute;
  right: 20px;
  color: #94a3b8;
  font-size: 0.9em;
  background: rgba(30, 41, 59, 0.9);
  padding: 8px 15px;
  border-radius: 6px;
  box-shadow: 0 4px 6px rgba(0, 0, 0, 0.1);
  border: 1px solid rgba(255, 255, 255, 0.1);
  backdrop-filter: blur(8px);
}

/* 自定义滚动条样式 */
.metric-details::-webkit-scrollbar {
  width: 6px;
  height: 6px;
}

.metric-details::-webkit-scrollbar-track {
  background: rgba(30, 41, 59, 0.4);
  border-radius: 3px;
}

.metric-details::-webkit-scrollbar-thumb {
  background: rgba(59, 130, 246, 0.5);
  border-radius: 3px;
}

.metric-details::-webkit-scrollbar-thumb:hover {
  background: rgba(59, 130, 246, 0.7);
}

/* 响应式布局 */
@media (max-width: 1600px) {
  .metrics-container {
    grid-template-columns: repeat(3, 1fr);
    grid-template-rows: auto;
  }
}

@media (max-width: 1200px) {
  .metrics-container {
    grid-template-columns: repeat(2, 1fr);
    grid-template-rows: auto;
  }
  
  .metric-details {
    grid-template-columns: 1fr;
  }
}

@media (max-width: 768px) {
  .metrics-container {
    grid-template-columns: 1fr;
    padding: 15px;
  }
  
  .metric-card {
    padding: 20px;
  }
  
  .metric-value {
    font-size: 2.5em;
  }
  
  .detail-item {
    padding: 12px 15px;
  }

  .header-container {
    flex-direction: column;
    gap: 15px;
    padding: 15px;
  }

  .last-update {
    position: static;
    margin-top: 10px;
  }

  h1 {
    text-align: center;
    font-size: 1.8em;
  }
}
</style> 