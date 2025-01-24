#!/bin/bash

# 目标 URL
url="127.0.0.1:8888"

# 并发请求数
concurrent_requests=1000

# 请求总数
request_count=10

# 使用 seq 和 xargs 的 -I 选项
seq 1 $request_count | xargs -P $concurrent_requests -I {} curl -s "${url}?request={}" > /dev/null
