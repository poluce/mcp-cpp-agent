// test_mcp_server.js
// 这是一个极简的、零依赖的 Mock MCP Stdio 服务端，用于验证 C++ MCP SDK 功能
const readline = require('readline');

const rl = readline.createInterface({
  input: process.stdin,
  output: process.stdout,
  terminal: false
});

// 支持的工具列表
const TOOLS = [
  {
    name: "calculate_add",
    description: "计算两个数字的和",
    inputSchema: {
      type: "object",
      properties: {
        a: { type: "number", description: "第一个数字" },
        b: { type: "number", description: "第二个数字" }
      },
      required: ["a", "b"]
    }
  },
  {
    name: "get_system_info",
    description: "获取系统版本及当前状态",
    inputSchema: {
      type: "object",
      properties: {}
    }
  }
];

rl.on('line', (line) => {
  if (!line.trim()) return;
  try {
    const request = JSON.parse(line);
    const { id, method, params } = request;

    if (id === undefined) return; // 忽略通知

    let response = {
      jsonrpc: "2.0",
      id: id
    };

    if (method === 'initialize') {
      response.result = {
        protocolVersion: "2024-11-05",
        capabilities: {
          tools: { listChanged: false }
        },
        serverInfo: {
          name: "mock-test-server",
          version: "1.0.0"
        }
      };
    } else if (method === 'tools/list') {
      response.result = { tools: TOOLS };
    } else if (method === 'tools/call') {
      const toolName = params.name;
      const args = params.arguments || {};
      
      if (toolName === 'calculate_add') {
        const a = Number(args.a || 0);
        const b = Number(args.b || 0);
        response.result = {
          content: [{ type: "text", text: `计算成功，结果为: ${a + b}` }]
        };
      } else if (toolName === 'get_system_info') {
        response.result = {
          content: [{ type: "text", text: `Mock 操作系统版本: Windows 11 PRO (Mocked by JS), 运行正常` }]
        };
      } else {
        response.error = { code: -32601, message: `Tool not found: ${toolName}` };
      }
    } else {
      response.error = { code: -32601, message: `Method not found: ${method}` };
    }

    process.stdout.write(JSON.stringify(response) + "\n");
  } catch (err) {
    process.stderr.write(`Error: ${err.message}\n`);
  }
});
