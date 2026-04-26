# 自研 HTTP 服务框架（后续扩展 AI 应用）

本项目目标：
- 在 WSL 环境中，从零实现一个模块化的 C++ HTTP 框架
- 采用分批次迭代开发，每一批都可编译、可运行、可提交
- 后续在该框架基础上接入 AI 应用能力（图片/推理服务等）

## 当前阶段（HTTP 基础）

目前已完成：
1. `HttpResponse` 的基础构建与序列化
2. `Router` 的最小路由分发能力（`GET` / `POST` + `404` 回退）
3. `HttpContext` 的基础 HTTP 请求解析（请求行、请求头、body）
4. 动态路由参数匹配（例如 `/users/:id`）
5. 基于 TCP socket 的最小 HTTP 网络闭环（接收请求 -> 解析 -> 路由 -> 响应）

## 项目结构

- `src/` 源码目录
- `include/` 头文件目录
- `build/` 编译输出目录
- `CMakeLists.txt` 构建脚本

## 构建与运行

```bash
cmake -S . -B build
cmake --build build
./build/muduo_http_server
```

启动后可用另一个终端测试：

```bash
curl http://127.0.0.1:8080/
curl http://127.0.0.1:8080/health
curl http://127.0.0.1:8080/users/42
```

当前示例接口：

- `GET /` 返回框架欢迎文本
- `GET /health` 返回健康检查结果
- `GET /users/:id` 演示动态路由参数匹配

## 分批次开发约定

- 每次只做一个小目标
- 每次完成后保证：可编译、可运行、可提交
- 提交信息清晰描述本批次范围，避免一次改动过多

建议提交流程：

```bash
git add .
git commit -m "feat(http): <本批次功能描述>"
git push
```
