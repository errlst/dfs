import { defineStore } from 'pinia'
import { config } from '../config/config'
import axios from 'axios'

export const usePerformanceStore = defineStore('performance', {
  state: () => ({
    masterNode: {
      // CPU相关
      cpu: 0,
      cpuCores: 0,
      load1: 0,
      load5: 0,
      load15: 0,
      
      // 内存相关
      memory: 0,
      totalMemory: 0,
      availableMemory: 0,
      
      // 网络相关
      network: 0,
      packetsRecv: 0,
      packetsSend: 0,
      errin: 0,
      errout: 0,
      dropin: 0,
      dropout: 0,
      
      // 系统信息
      ip: '',
      port: 0,
      basePath: '',
      storageGroupSize: 0,
      storageCount: 0,
      threadCount: 0,
      
      // 请求统计
      connectionCount: 0,
      countConcurrent: 0,
      countLastSecond: { total: 0, success: 0, peak: 0 },
      countLastMinute: { total: 0, success: 0, peak: 0 },
      countLastHour: { total: 0, success: 0, peak: 0 },
      countLastDay: { total: 0, success: 0, peak: 0 }
    },
    groups: [],
    lastUpdate: null,
    isLoading: false,
    error: null
  }),
  
  actions: {
    async fetchMasterNodeData() {
      this.isLoading = true
      try {
        console.log('开始获取数据，API地址:', config.api.baseURL)
        const response = await axios.get(config.api.baseURL)
        console.log('API响应:', response)
        
        const data = response.data
        if (!data) {
          throw new Error('返回数据为空')
        }
        
        console.log('原始数据:', data)
        
        // 处理主节点数据
        const masterNodeData = {
          // CPU信息
          cpu: data.cpu?.usage_percent ? Math.round(data.cpu.usage_percent) : 0,
          cpuCores: data.cpu?.core_usages?.length || 0,
          load1: data.cpu?.load_1 || 0,
          load5: data.cpu?.load_5 || 0,
          load15: data.cpu?.load_15 || 0,
          
          // 内存信息
          memory: data.mem?.available && data.mem?.total 
            ? Math.round((1 - data.mem.available / data.mem.total) * 100) 
            : 0,
          totalMemory: data.mem?.total || 0,
          availableMemory: data.mem?.available || 0,
          
          // 网络信息
          network: data.net?.total_recv && data.net?.total_send
            ? Math.round((data.net.total_recv + data.net.total_send) / 1024 / 1024)
            : 0,
          packetsRecv: data.net?.packets_recv || 0,
          packetsSend: data.net?.packets_send || 0,
          errin: data.net?.errin || 0,
          errout: data.net?.errout || 0,
          dropin: data.net?.dropin || 0,
          dropout: data.net?.dropout || 0,
          
          // 系统信息
          ip: data.master_info?.ip || '',
          port: data.master_info?.port || 0,
          basePath: data.master_info?.base_path || '',
          storageGroupSize: data.master_info?.storage_group_size || 0,
          storageCount: data.master_info?.storage_count || 0,
          threadCount: data.master_info?.thread_count || 0,
          
          // 请求统计
          connectionCount: data.request?.connection_count || 0,
          countConcurrent: data.request?.count_concurrent || 0,
          countLastSecond: data.request?.count_last_second || { total: 0, success: 0, peak: 0 },
          countLastMinute: data.request?.count_last_minute || { total: 0, success: 0, peak: 0 },
          countLastHour: data.request?.count_last_hour || { total: 0, success: 0, peak: 0 },
          countLastDay: data.request?.count_last_day || { total: 0, success: 0, peak: 0 }
        }
        
        console.log('处理后的主节点数据:', masterNodeData)
        this.updateMasterNode(masterNodeData)
        
        // 处理存储节点数据
        const groups = []
        if (data.storage_metrics) {
          for (const [groupId, nodes] of Object.entries(data.storage_metrics)) {
            if (Array.isArray(nodes)) {
              groups.push({
                id: groupId,
                nodes: nodes.map(node => ({
                  id: node.storage_info?.id || 'unknown',
                  ip: node.storage_info?.ip || 'unknown',
                  port: node.storage_info?.port || 0,
                  cpu: node.cpu?.usage_percent ? Math.round(node.cpu.usage_percent) : 0,
                  memory: node.mem?.available && node.mem?.total
                    ? Math.round((1 - node.mem.available / node.mem.total) * 100)
                    : 0,
                  network: node.net?.total_recv && node.net?.total_send
                    ? Math.round((node.net.total_recv + node.net.total_send) / 1024 / 1024)
                    : 0,
                  hotStorage: {
                    total: node.storage_info?.store_infos?.hot?.[0]?.total_space || 0,
                    free: node.storage_info?.store_infos?.hot?.[0]?.free_space || 0
                  },
                  coldStorage: {
                    total: node.storage_info?.store_infos?.cold?.[0]?.total_space || 0,
                    free: node.storage_info?.store_infos?.cold?.[0]?.free_space || 0
                  }
                }))
              })
            }
          }
        }
        
        console.log('处理后的存储节点数据:', groups)
        this.updateGroups(groups)
        
        this.lastUpdate = new Date()
        this.error = null
      } catch (err) {
        console.error('数据获取失败:', err)
        this.error = err.message || '数据获取失败'
        // 保持现有数据不变，避免清空
      } finally {
        this.isLoading = false
      }
    },
    
    updateMasterNode(data) {
      this.masterNode = { ...this.masterNode, ...data }
    },
    
    updateGroups(data) {
      this.groups = data
    },
    
    // 获取性能指标的状态（正常、警告、危险）
    getMetricStatus(metric, value) {
      const thresholds = config.thresholds[metric]
      if (!thresholds) return 'normal'
      
      if (value >= thresholds.danger) return 'danger'
      if (value >= thresholds.warning) return 'warning'
      return 'normal'
    }
  }
}) 