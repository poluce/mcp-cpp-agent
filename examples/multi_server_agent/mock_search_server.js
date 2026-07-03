const readline = require('readline');

const rl = readline.createInterface({
  input: process.stdin,
  output: process.stdout,
  terminal: false
});

rl.on('line', (line) => {
  try {
    const request = JSON.parse(line);
    if (request.method === 'initialize') {
      const response = {
        jsonrpc: "2.0",
        id: request.id,
        result: {
          protocolVersion: "2025-11-25",
          capabilities: {
            tools: {}
          },
          serverInfo: {
            name: "mock-search-server",
            version: "1.0.0"
          }
        }
      };
      console.log(JSON.stringify(response));
    } else if (request.method === 'tools/list') {
      const response = {
        jsonrpc: "2.0",
        id: request.id,
        result: {
          tools: [
            {
              name: "search_items",
              description: "在模拟数据库中执行搜索",
              inputSchema: {
                type: "object",
                properties: {
                  query: { type: "string", description: "搜索关键词" }
                },
                required: ["query"]
              }
            }
          ]
        }
      };
      console.log(JSON.stringify(response));
    } else if (request.method === 'tools/call') {
      const toolName = request.params.name;
      const args = request.params.arguments || {};
      let content = [];
      
      if (toolName === 'search_items') {
        const query = args.query || "";
        content.push({
          type: "text",
          text: `搜索结果: 成功找到与 "${query}" 相关的 3 条记录！`
        });
      }
      
      const response = {
        jsonrpc: "2.0",
        id: request.id,
        result: {
          content: content,
          isError: false
        }
      };
      console.log(JSON.stringify(response));
    }
  } catch (e) {
    // 忽略解析错误
  }
});
