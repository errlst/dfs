<script setup>
import { defineProps } from 'vue'

const props = defineProps({
  node: {
    type: Object,
    required: true
  },
  visible: {
    type: Boolean,
    required: true
  }
})

const emit = defineEmits(['close'])

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
</script>

<template>
  <div v-if="visible" class="modal-overlay" @click="emit('close')">
    <div class="modal-content" @click.stop>
      <div class="modal-header">
        <h2>节点详情</h2>
        <button class="close-button" @click="emit('close')">×</button>
      </div>
      
      <div class="modal-body">
        <div class="detail-section">
          <h3>基本信息</h3>
          <div class="detail-grid">
            <div class="detail-item">
              <span class="label">节点ID:</span>
              <span class="value">{{ node.id }}</span>
            </div>
            <div class="detail-item">
              <span class="label">IP地址:</span>
              <span class="value">{{ node.ip }}</span>
            </div>
            <div class="detail-item">
              <span class="label">端口:</span>
              <span class="value">{{ node.port }}</span>
            </div>
          </div>
        </div>

        <div class="detail-section">
          <h3>性能指标</h3>
          <div class="detail-grid">
            <div class="detail-item">
              <span class="label">CPU使用率:</span>
              <span class="value">{{ node.cpu }}%</span>
            </div>
            <div class="detail-item">
              <span class="label">内存使用率:</span>
              <span class="value">{{ node.memory }}%</span>
            </div>
            <div class="detail-item">
              <span class="label">网络传输速率:</span>
              <span class="value">{{ node.network }} MB/s</span>
            </div>
          </div>
        </div>

        <div class="detail-section">
          <h3>存储信息</h3>
          <div class="storage-info">
            <div class="storage-type">
              <h4>热存储</h4>
              <div class="storage-bar-container">
                <div 
                  class="storage-bar"
                  :style="{ width: calculateUsagePercent(node.hotStorage.total, node.hotStorage.free) + '%' }"
                ></div>
              </div>
              <div class="storage-details">
                <div class="detail-item">
                  <span class="label">总容量:</span>
                  <span class="value">{{ formatBytes(node.hotStorage.total) }}</span>
                </div>
                <div class="detail-item">
                  <span class="label">已用空间:</span>
                  <span class="value">{{ formatBytes(node.hotStorage.total - node.hotStorage.free) }}</span>
                </div>
                <div class="detail-item">
                  <span class="label">剩余空间:</span>
                  <span class="value">{{ formatBytes(node.hotStorage.free) }}</span>
                </div>
                <div class="detail-item">
                  <span class="label">使用率:</span>
                  <span class="value">{{ calculateUsagePercent(node.hotStorage.total, node.hotStorage.free) }}%</span>
                </div>
              </div>
            </div>

            <div class="storage-type">
              <h4>冷存储</h4>
              <div class="storage-bar-container">
                <div 
                  class="storage-bar"
                  :style="{ width: calculateUsagePercent(node.coldStorage.total, node.coldStorage.free) + '%' }"
                ></div>
              </div>
              <div class="storage-details">
                <div class="detail-item">
                  <span class="label">总容量:</span>
                  <span class="value">{{ formatBytes(node.coldStorage.total) }}</span>
                </div>
                <div class="detail-item">
                  <span class="label">已用空间:</span>
                  <span class="value">{{ formatBytes(node.coldStorage.total - node.coldStorage.free) }}</span>
                </div>
                <div class="detail-item">
                  <span class="label">剩余空间:</span>
                  <span class="value">{{ formatBytes(node.coldStorage.free) }}</span>
                </div>
                <div class="detail-item">
                  <span class="label">使用率:</span>
                  <span class="value">{{ calculateUsagePercent(node.coldStorage.total, node.coldStorage.free) }}%</span>
                </div>
              </div>
            </div>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.modal-overlay {
  position: fixed;
  top: 0;
  left: 0;
  right: 0;
  bottom: 0;
  background: rgba(0, 0, 0, 0.7);
  display: flex;
  justify-content: center;
  align-items: center;
  z-index: 1000;
  backdrop-filter: blur(5px);
}

.modal-content {
  background: #1e293b;
  border-radius: 12px;
  width: 90%;
  max-width: 800px;
  max-height: 90vh;
  overflow-y: auto;
  box-shadow: 0 8px 32px rgba(0, 0, 0, 0.3);
  border: 1px solid rgba(255, 255, 255, 0.1);
}

.modal-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 20px;
  border-bottom: 1px solid rgba(255, 255, 255, 0.1);
}

.modal-header h2 {
  margin: 0;
  color: #e2e8f0;
  font-size: 1.5em;
}

.close-button {
  background: none;
  border: none;
  color: #94a3b8;
  font-size: 24px;
  cursor: pointer;
  padding: 0;
  width: 32px;
  height: 32px;
  display: flex;
  align-items: center;
  justify-content: center;
  border-radius: 50%;
  transition: all 0.3s ease;
}

.close-button:hover {
  background: rgba(255, 255, 255, 0.1);
  color: #e2e8f0;
}

.modal-body {
  padding: 20px;
}

.detail-section {
  margin-bottom: 30px;
}

.detail-section h3 {
  color: #e2e8f0;
  margin: 0 0 15px 0;
  font-size: 1.2em;
  padding-bottom: 8px;
  border-bottom: 1px solid rgba(255, 255, 255, 0.1);
}

.detail-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(200px, 1fr));
  gap: 15px;
}

.detail-item {
  background: rgba(30, 41, 59, 0.4);
  padding: 12px;
  border-radius: 8px;
  border: 1px solid rgba(255, 255, 255, 0.05);
}

.detail-item .label {
  color: #94a3b8;
  font-size: 0.9em;
  display: block;
  margin-bottom: 4px;
}

.detail-item .value {
  color: #e2e8f0;
  font-size: 1.1em;
  font-weight: 500;
}

.storage-info {
  display: grid;
  gap: 20px;
}

.storage-type {
  background: rgba(30, 41, 59, 0.4);
  padding: 15px;
  border-radius: 8px;
  border: 1px solid rgba(255, 255, 255, 0.05);
}

.storage-type h4 {
  color: #e2e8f0;
  margin: 0 0 15px 0;
  font-size: 1.1em;
}

.storage-bar-container {
  height: 8px;
  background: rgba(30, 41, 59, 0.6);
  border-radius: 4px;
  overflow: hidden;
  margin-bottom: 15px;
}

.storage-bar {
  height: 100%;
  background: #3b82f6;
  transition: width 0.3s ease;
}

.storage-details {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(180px, 1fr));
  gap: 10px;
}

@media (max-width: 640px) {
  .modal-content {
    width: 95%;
    margin: 10px;
  }

  .detail-grid {
    grid-template-columns: 1fr;
  }

  .storage-details {
    grid-template-columns: 1fr;
  }
}
</style> 