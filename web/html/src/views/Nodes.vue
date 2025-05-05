<script setup>
import { usePerformanceStore } from '../stores/performance'
import { useRouter } from 'vue-router'
import { onMounted, onUnmounted, ref } from 'vue'
import { config } from '../config/config'
import NodeDetailModal from '../components/NodeDetailModal.vue'

const store = usePerformanceStore()
const router = useRouter()
let updateTimer = null

const goBack = () => {
  router.push('/')
}

const fetchData = async () => {
  await store.fetchMasterNodeData()
}

onMounted(() => {
  fetchData()
  updateTimer = setInterval(fetchData, config.updateInterval)
})

onUnmounted(() => {
  if (updateTimer) {
    clearInterval(updateTimer)
  }
})

const formatBytes = (bytes) => {
  if (bytes === 0) return '0 B'
  const k = 1024
  const sizes = ['B', 'KB', 'MB', 'GB', 'TB']
  const i = Math.floor(Math.log(bytes) / Math.log(k))
  return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i]
}

const getStatusClass = (metric, value) => {
  return `status-${store.getMetricStatus(metric, value)}`
}

const calculateUsagePercent = (total, free) => {
  if (total === 0) return 0
  return Math.round(((total - free) / total) * 100)
}

const selectedNode = ref(null)
const showNodeDetail = ref(false)

const getNodeStatus = (node) => {
  // 检查所有关键指标
  const metrics = [
    { type: 'cpu', value: node.cpu },
    { type: 'memory', value: node.memory },
    { type: 'network', value: node.network },
    { type: 'storage', value: calculateUsagePercent(node.hotStorage.total, node.hotStorage.free) },
    { type: 'storage', value: calculateUsagePercent(node.coldStorage.total, node.coldStorage.free) }
  ]

  // 如果任何一个指标处于危险状态，整个节点就是危险状态
  if (metrics.some(m => store.getMetricStatus(m.type, m.value) === 'danger')) {
    return 'danger'
  }
  // 如果任何一个指标处于警告状态，整个节点就是警告状态
  if (metrics.some(m => store.getMetricStatus(m.type, m.value) === 'warning')) {
    return 'warning'
  }
  // 否则就是正常状态
  return 'normal'
}

const openNodeDetail = (node) => {
  selectedNode.value = node
  showNodeDetail.value = true
}

const closeNodeDetail = () => {
  showNodeDetail.value = false
  selectedNode.value = null
}
</script>

<template>
  <div class="nodes-container">
    <div class="header">
      <h1>存储节点监控</h1>
      <button @click="goBack" class="back-btn">返回主页面</button>
    </div>

    <div v-if="store.error" class="error-message">
      获取数据失败: {{ store.error }}
    </div>

    <div v-else-if="store.groups.length === 0" class="empty-state">
      暂无存储节点数据
    </div>

    <div v-else class="groups-container">
      <div v-for="group in store.groups" :key="group.id" class="group-card">
        <h2 class="group-title">存储组 {{ group.id }}</h2>
        
        <div class="nodes-grid">
          <div 
            v-for="node in group.nodes" 
            :key="node.id" 
            class="node-card"
            :class="'node-' + getNodeStatus(node)"
            @click="openNodeDetail(node)"
          >
            <div class="node-header">
              <h3>节点 {{ node.id }}</h3>
              <div class="node-info">
                <span>{{ node.ip }}:{{ node.port }}</span>
              </div>
            </div>

            <div class="metrics-grid">
              <!-- CPU 指标 -->
              <div class="metric-item" :class="getStatusClass('cpu', node.cpu)">
                <div class="metric-label">CPU</div>
                <div class="metric-value">{{ node.cpu }}%</div>
              </div>

              <!-- 内存指标 -->
              <div class="metric-item" :class="getStatusClass('memory', node.memory)">
                <div class="metric-label">内存</div>
                <div class="metric-value">{{ node.memory }}%</div>
              </div>

              <!-- 网络指标 -->
              <div class="metric-item" :class="getStatusClass('network', node.network)">
                <div class="metric-label">网络</div>
                <div class="metric-value">{{ node.network }} MB/s</div>
              </div>
            </div>

            <div class="storage-section">
              <!-- 热存储 -->
              <div class="storage-item">
                <h4>热存储</h4>
                <div class="storage-bar-container">
                  <div 
                    class="storage-bar" 
                    :class="getStatusClass('storage', calculateUsagePercent(node.hotStorage.total, node.hotStorage.free))"
                    :style="{ width: calculateUsagePercent(node.hotStorage.total, node.hotStorage.free) + '%' }"
                  ></div>
                </div>
                <div class="storage-details">
                  <span>已用: {{ formatBytes(node.hotStorage.total - node.hotStorage.free) }}</span>
                  <span>总容量: {{ formatBytes(node.hotStorage.total) }}</span>
                </div>
              </div>

              <!-- 冷存储 -->
              <div class="storage-item">
                <h4>冷存储</h4>
                <div class="storage-bar-container">
                  <div 
                    class="storage-bar" 
                    :class="getStatusClass('storage', calculateUsagePercent(node.coldStorage.total, node.coldStorage.free))"
                    :style="{ width: calculateUsagePercent(node.coldStorage.total, node.coldStorage.free) + '%' }"
                  ></div>
                </div>
                <div class="storage-details">
                  <span>已用: {{ formatBytes(node.coldStorage.total - node.coldStorage.free) }}</span>
                  <span>总容量: {{ formatBytes(node.coldStorage.total) }}</span>
                </div>
              </div>
            </div>
          </div>
        </div>
      </div>
    </div>

    <!-- 添加节点详情弹窗 -->
    <NodeDetailModal
      v-if="selectedNode"
      :node="selectedNode"
      :visible="showNodeDetail"
      @close="closeNodeDetail"
    />

    <div class="last-update" v-if="store.lastUpdate">
      最后更新: {{ store.lastUpdate.toLocaleTimeString() }}
    </div>
  </div>
</template>

<style scoped>
.nodes-container {
  padding: 20px;
  min-height: 100vh;
  width: 100vw;
  max-width: 100%;
  background: linear-gradient(135deg, #0f172a 0%, #1e293b 100%);
  color: #e2e8f0;
  box-sizing: border-box;
  overflow: auto;
  position: absolute;
  left: 0;
  right: 0;
  background-image: 
    radial-gradient(circle at 10% 20%, rgba(59, 130, 246, 0.1) 0%, transparent 20%),
    radial-gradient(circle at 90% 80%, rgba(239, 68, 68, 0.1) 0%, transparent 20%),
    radial-gradient(circle at 50% 50%, rgba(245, 158, 11, 0.1) 0%, transparent 30%),
    linear-gradient(135deg, #0f172a 0%, #1e293b 100%);
}

.header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 30px;
  padding: 20px;
  background: rgba(30, 41, 59, 0.95);
  border-radius: 16px;
  box-shadow: 0 8px 32px rgba(0, 0, 0, 0.2);
  backdrop-filter: blur(12px);
  border: 1px solid rgba(59, 130, 246, 0.2);
}

h1 {
  margin: 0;
  font-size: 2.2em;
  color: #e2e8f0;
  text-transform: uppercase;
  letter-spacing: 3px;
  font-weight: 600;
  position: relative;
  padding-bottom: 10px;
}

h1::after {
  content: '';
  position: absolute;
  bottom: 0;
  left: 0;
  width: 100px;
  height: 3px;
  background: linear-gradient(90deg, #3b82f6, transparent);
  animation: gradientFlow 3s infinite;
}

@keyframes gradientFlow {
  0% { background-position: 100% 0; }
  50% { background-position: 0 0; }
  100% { background-position: 100% 0; }
}

.back-btn {
  padding: 12px 25px;
  background: linear-gradient(135deg, rgba(59, 130, 246, 0.9) 0%, rgba(37, 99, 235, 0.9) 100%);
  color: white;
  border: none;
  border-radius: 12px;
  cursor: pointer;
  font-size: 1.1em;
  font-weight: 600;
  letter-spacing: 1px;
  transition: all 0.3s cubic-bezier(0.4, 0, 0.2, 1);
  text-transform: uppercase;
  position: relative;
  overflow: hidden;
}

.back-btn::before {
  content: '';
  position: absolute;
  top: -50%;
  left: -50%;
  width: 200%;
  height: 200%;
  background: radial-gradient(circle, rgba(255, 255, 255, 0.2) 0%, transparent 60%);
  transform: rotate(45deg);
  transition: 0.5s;
}

.back-btn:hover {
  transform: translateY(-3px);
  box-shadow: 0 10px 20px rgba(37, 99, 235, 0.3);
}

.back-btn:hover::before {
  transform: rotate(45deg) translateY(100%);
}

.groups-container {
  display: flex;
  flex-direction: column;
  gap: 30px;
  max-height: calc(100vh - 140px);
  overflow-y: auto;
  padding-right: 10px;
}

.groups-container::-webkit-scrollbar {
  width: 8px;
}

.groups-container::-webkit-scrollbar-track {
  background: rgba(30, 41, 59, 0.4);
  border-radius: 4px;
}

.groups-container::-webkit-scrollbar-thumb {
  background: rgba(59, 130, 246, 0.5);
  border-radius: 4px;
}

.groups-container::-webkit-scrollbar-thumb:hover {
  background: rgba(59, 130, 246, 0.7);
}

.group-card {
  background: rgba(30, 41, 59, 0.8);
  border-radius: 16px;
  padding: 25px;
  box-shadow: 0 8px 32px rgba(0, 0, 0, 0.2);
  backdrop-filter: blur(12px);
  border: 1px solid rgba(255, 255, 255, 0.1);
  transition: all 0.4s cubic-bezier(0.4, 0, 0.2, 1);
}

.group-card:hover {
  transform: translateY(-5px);
  box-shadow: 0 12px 40px rgba(0, 0, 0, 0.3);
  border-color: rgba(59, 130, 246, 0.3);
}

.group-title {
  margin: 0 0 25px 0;
  color: #e2e8f0;
  font-size: 1.8em;
  padding-bottom: 15px;
  border-bottom: 2px solid rgba(59, 130, 246, 0.5);
  text-transform: uppercase;
  letter-spacing: 2px;
  font-weight: 600;
  text-align: center;
  position: relative;
}

.group-title::after {
  content: '';
  position: absolute;
  bottom: -2px;
  left: 50%;
  transform: translateX(-50%);
  width: 100px;
  height: 2px;
  background: linear-gradient(90deg, transparent, #3b82f6, transparent);
  animation: gradientFlow 3s infinite;
}

.nodes-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(350px, 1fr));
  gap: 20px;
  max-height: 80vh;
  overflow-y: auto;
  padding-right: 10px;
}

.nodes-grid::-webkit-scrollbar {
  width: 6px;
}

.nodes-grid::-webkit-scrollbar-track {
  background: rgba(30, 41, 59, 0.4);
  border-radius: 3px;
}

.nodes-grid::-webkit-scrollbar-thumb {
  background: rgba(59, 130, 246, 0.5);
  border-radius: 3px;
}

.nodes-grid::-webkit-scrollbar-thumb:hover {
  background: rgba(59, 130, 246, 0.7);
}

.node-card {
  background: rgba(30, 41, 59, 0.6);
  border-radius: 12px;
  padding: 20px;
  border: 1px solid rgba(255, 255, 255, 0.08);
  transition: all 0.4s cubic-bezier(0.4, 0, 0.2, 1);
  cursor: pointer;
  position: relative;
  overflow: hidden;
  backdrop-filter: blur(8px);
}

.node-card:hover {
  transform: translateY(-5px) scale(1.02);
  box-shadow: 0 12px 30px rgba(0, 0, 0, 0.25);
  border-color: rgba(59, 130, 246, 0.3);
  z-index: 1;
}

.node-card::before {
  content: '';
  position: absolute;
  top: 0;
  left: 0;
  width: 4px;
  height: 100%;
  transition: all 0.3s ease;
}

.node-normal::before {
  background: linear-gradient(to bottom, #10b981, #059669);
  box-shadow: 0 0 15px rgba(16, 185, 129, 0.3);
}

.node-warning::before {
  background: linear-gradient(to bottom, #f59e0b, #d97706);
  box-shadow: 0 0 15px rgba(245, 158, 11, 0.3);
}

.node-danger::before {
  background: linear-gradient(to bottom, #ef4444, #dc2626);
  box-shadow: 0 0 15px rgba(239, 68, 68, 0.3);
}

.node-danger {
  background: rgba(239, 68, 68, 0.15);
  animation: dangerPulse 2s infinite;
}

.node-header {
  margin-bottom: 15px;
}

.node-header h3 {
  margin: 0 0 10px 0;
  color: #e2e8f0;
  font-size: 1.4em;
  font-weight: 600;
  letter-spacing: 1px;
}

.node-info {
  color: #94a3b8;
  font-size: 1em;
  opacity: 0.9;
}

.metrics-grid {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 15px;
  margin: 20px 0;
}

.metric-item {
  padding: 15px;
  border-radius: 10px;
  background: rgba(30, 41, 59, 0.8);
  text-align: center;
  border: 1px solid rgba(255, 255, 255, 0.1);
  transition: all 0.3s ease;
}

.metric-item:hover {
  transform: translateY(-3px);
  box-shadow: 0 8px 20px rgba(0, 0, 0, 0.2);
  border-color: rgba(59, 130, 246, 0.3);
}

.metric-label {
  font-size: 0.9em;
  color: #94a3b8;
  margin-bottom: 5px;
}

.metric-value {
  font-size: 1.2em;
  font-weight: bold;
  color: #e2e8f0;
}

.storage-section {
  display: flex;
  flex-direction: column;
  gap: 20px;
}

.storage-item {
  background: rgba(30, 41, 59, 0.8);
  padding: 20px;
  border-radius: 12px;
  border: 1px solid rgba(255, 255, 255, 0.1);
  transition: all 0.3s ease;
}

.storage-item:hover {
  transform: translateY(-3px);
  box-shadow: 0 8px 20px rgba(0, 0, 0, 0.2);
  border-color: rgba(59, 130, 246, 0.3);
}

.storage-item h4 {
  margin: 0 0 10px 0;
  color: #e2e8f0;
  font-size: 1em;
}

.storage-bar-container {
  height: 10px;
  background: rgba(30, 41, 59, 0.6);
  border-radius: 5px;
  overflow: hidden;
  margin: 15px 0;
  box-shadow: inset 0 2px 4px rgba(0, 0, 0, 0.2);
}

.storage-bar {
  height: 100%;
  background: linear-gradient(90deg, #3b82f6, #2563eb);
  transition: all 0.3s ease;
  position: relative;
  overflow: hidden;
}

.storage-bar::after {
  content: '';
  position: absolute;
  top: 0;
  left: 0;
  right: 0;
  bottom: 0;
  background: linear-gradient(
    90deg,
    rgba(255, 255, 255, 0) 0%,
    rgba(255, 255, 255, 0.2) 50%,
    rgba(255, 255, 255, 0) 100%
  );
  animation: shimmer 2s infinite;
}

.storage-details {
  display: grid;
  grid-template-columns: repeat(2, 1fr);
  gap: 12px;
}

.status-normal {
  border-left: 3px solid #10b981;
}

.status-warning {
  border-left: 3px solid #f59e0b;
}

.status-danger {
  border-left: 3px solid #ef4444;
  animation: dangerPulse 2s infinite;
}

.storage-bar.status-normal {
  background: #10b981;
}

.storage-bar.status-warning {
  background: #f59e0b;
}

.storage-bar.status-danger {
  background: #ef4444;
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

.error-message {
  background: rgba(239, 68, 68, 0.2);
  color: #ef4444;
  padding: 20px;
  border-radius: 8px;
  margin: 20px;
  text-align: center;
  border: 1px solid rgba(239, 68, 68, 0.3);
}

.empty-state {
  text-align: center;
  padding: 40px;
  color: #94a3b8;
  font-size: 1.2em;
}

.last-update {
  text-align: center;
  color: #94a3b8;
  margin: 20px auto;
  font-size: 0.95em;
  background: rgba(30, 41, 59, 0.8);
  padding: 12px;
  border-radius: 8px;
  box-shadow: 0 4px 12px rgba(0, 0, 0, 0.15);
  border: 1px solid rgba(255, 255, 255, 0.1);
  backdrop-filter: blur(8px);
  max-width: 400px;
}

@media (max-width: 768px) {
  .header {
    flex-direction: column;
    gap: 15px;
    text-align: center;
  }

  .nodes-grid {
    grid-template-columns: 1fr;
  }

  .metrics-grid {
    grid-template-columns: 1fr;
  }
}
</style> 