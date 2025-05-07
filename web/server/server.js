const http = require("http");
const fs = require("fs");
const path = require("path");

// 配置参数
const PORT = 3000;
const FILE_PATH = "/home/errlst/dfs/build/base_path/master/data/metrics.json";

// 创建HTTP服务器
const server = http.createServer(async (req, res) => {
  try {
    // 读取文件内容
    const data = await fs.promises.readFile(FILE_PATH, "utf8");

    // 设置响应头
    res.writeHead(200, {
      "Content-Type": "application/json",
      "Access-Control-Allow-Origin": "*", // 如果需要跨域访问
    });

    // 发送文件内容
    res.end(data);
  } catch (err) {
    // 错误处理
    console.error("Error reading file:", err);
    res.writeHead(500, { "Content-Type": "text/plain" });
    res.end("Internal Server Error");
  }
});

// 启动服务器
server.listen(PORT, () => {
  console.log(`Server running at http://localhost:${PORT}/`);
  console.log(`Serving file from: ${FILE_PATH}`);
});
