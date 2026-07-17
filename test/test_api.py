#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
serverinfo-rest API 测试脚本
用法: python test_api.py --host <host> --port <port> --token <token>
"""

import argparse
import json
import sys
from urllib.request import urlopen, Request
from urllib.error import URLError, HTTPError


def colored(text: str, color: str) -> str:
    """简单的终端颜色支持"""
    colors = {
        "red": "\033[91m",
        "green": "\033[92m",
        "yellow": "\033[93m",
        "blue": "\033[94m",
        "magenta": "\033[95m",
        "cyan": "\033[96m",
        "reset": "\033[0m",
    }
    return f"{colors.get(color, '')}{text}{colors['reset']}"


def print_header(text: str):
    print(colored("=" * 60, "cyan"))
    print(colored(f"  {text}", "cyan"))
    print(colored("=" * 60, "cyan"))


def print_section(emoji: str, title: str):
    print(colored(f"\n{emoji} {title}", "yellow"))
    print("-" * 40)


def request_api(
    url: str,
    timeout: int = 10,
    method: str = "GET",
    body: dict | None = None,
    bearer_token: str | None = None,
) -> tuple[int, dict | str | None]:
    """发送请求并返回 (状态码, 响应内容)。"""
    try:
        headers = {
            "User-Agent": "serverinfo-rest-tester/1.0",
            "Accept": "application/json",
        }
        if bearer_token:
            headers["Authorization"] = f"Bearer {bearer_token}"
        payload = None
        if body is not None:
            headers["Content-Type"] = "application/json"
            payload = json.dumps(body, ensure_ascii=False).encode("utf-8")
        req = Request(url, data=payload, headers=headers, method=method)
        with urlopen(req, timeout=timeout) as response:
            content = response.read().decode("utf-8")
            try:
                return response.status, json.loads(content)
            except json.JSONDecodeError:
                return response.status, content
    except HTTPError as e:
        try:
            content = e.read().decode("utf-8")
            return e.code, json.loads(content)
        except:
            return e.code, str(e)
    except URLError as e:
        return 0, f"连接失败: {e.reason}"
    except Exception as e:
        return 0, f"请求错误: {e}"


def print_response(status: int, data):
    """格式化打印响应"""
    if status == 0:
        print(colored(f"❌ {data}", "red"))
        return False
    
    status_color = "green" if 200 <= status < 300 else "red"
    print(f"状态码: {colored(str(status), status_color)}")
    
    if isinstance(data, dict):
        print(colored(json.dumps(data, indent=2, ensure_ascii=False), "green"))
    else:
        print(data)
    
    return 200 <= status < 300


def main():
    parser = argparse.ArgumentParser(
        description="serverinfo-rest API 测试脚本",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  python test_api.py --host localhost --port 60202
  python test_api.py --host 91.whzz.online --port 60202
  python test_api.py --host localhost --port 60202 --player Steve
  python test_api.py --host localhost --port 60202 --token your-secret-token
        """,
    )
    parser.add_argument("--host", default="localhost", help="服务器地址 (默认: localhost)")
    parser.add_argument("--port", type=int, default=60202, help="服务器端口 (默认: 60202)")
    parser.add_argument("--prefix", default="/api/v1", help="API 前缀 (默认: /api/v1)")
    parser.add_argument("--player", help="要查询的玩家名 (可选)")
    parser.add_argument("--token", help="访问令牌 (如果服务器启用了 token 认证)")
    parser.add_argument("--admin-token", help="管理 API 令牌；仅配合 --run-admin-tests 使用")
    parser.add_argument(
        "--run-admin-tests",
        action="store_true",
        help="显式运行会修改绑定和 BDS 白名单的管理接口测试",
    )
    parser.add_argument("--admin-player", help="管理接口测试专用玩家名，请勿使用真实授权玩家")
    parser.add_argument("--admin-command", default="list", help="管理接口测试命令 (默认: list)")
    parser.add_argument("--timeout", type=int, default=10, help="请求超时时间 (默认: 10秒)")
    
    args = parser.parse_args()
    
    base_url = f"http://{args.host}:{args.port}"
    api_base = f"{base_url}{args.prefix}"
    
    # 构建 token 查询参数
    token_param = f"token={args.token}" if args.token else ""
    
    def build_url(endpoint: str, extra_params: str = "") -> str:
        """构建带 token 的 URL"""
        params = []
        if extra_params:
            params.append(extra_params)
        if token_param:
            params.append(token_param)
        query = "&".join(params)
        return f"{endpoint}?{query}" if query else endpoint
    
    print_header("🧪 serverinfo-rest API 测试")
    print(f"🔗 Base URL: {colored(base_url, 'blue')}")
    print(f"🔗 API Base: {colored(api_base, 'blue')}")
    if args.token:
        print(f"🔑 Token: {colored('已配置', 'green')}")
    
    results = []
    
    # 测试 1: 根路径
    print_section("📍", "[1/7] 根路径 - API 概览")
    status, data = request_api(f"{base_url}/", args.timeout)
    results.append(("根路径", print_response(status, data)))
    
    # 测试 2: 健康检查 (不需要 token)
    print_section("❤️ ", "[2/7] 健康检查")
    status, data = request_api(f"{api_base}/health", args.timeout)
    results.append(("健康检查", print_response(status, data)))
    
    # 测试 3: 服务器状态
    print_section("📊", "[3/7] 服务器状态")
    status, data = request_api(build_url(f"{api_base}/status"), args.timeout)
    results.append(("服务器状态", print_response(status, data)))
    
    # 测试 4: 查在线聚合快照
    print_section("📈", "[4/8] 查在线聚合快照")
    status, data = request_api(build_url(f"{api_base}/overview"), args.timeout)
    results.append(("查在线聚合快照", print_response(status, data)))

    # 测试 5: 服务器信息
    print_section("🖥️ ", "[5/8] 服务器信息")
    status, data = request_api(build_url(f"{api_base}/server"), args.timeout)
    results.append(("服务器信息", print_response(status, data)))
    
    # 测试 5: 玩家列表
    print_section("👥", "[6/8] 玩家列表")
    status, data = request_api(build_url(f"{api_base}/players"), args.timeout)
    results.append(("玩家列表", print_response(status, data)))
    
    # 测试 6: 玩家数量
    print_section("🔢", "[7/8] 玩家数量")
    status, data = request_api(build_url(f"{api_base}/players/count"), args.timeout)
    results.append(("玩家数量", print_response(status, data)))
    
    # 测试 7: 玩家名列表
    print_section("📝", "[8/8] 玩家名列表")
    status, data = request_api(build_url(f"{api_base}/players/names"), args.timeout)
    results.append(("玩家名列表", print_response(status, data)))

    print_section("📚", "[9/10] 历史玩家分页")
    status, data = request_api(build_url(f"{api_base}/players/history", "page=1&pageSize=10"), args.timeout)
    results.append(("历史玩家分页", print_response(status, data)))
    
    # 测试 8: 查询指定玩家 (如果提供了玩家名)
    if args.player:
        print_section("👤", f"[额外] 查询玩家: {args.player}")
        status, data = request_api(build_url(f"{api_base}/player", f"name={args.player}"), args.timeout)
        results.append((f"玩家 {args.player}", print_response(status, data)))

        print_section("📈", f"[额外] 历史统计: {args.player}")
        status, data = request_api(
            build_url(f"{api_base}/players/stats", f"name={args.player}"),
            args.timeout,
        )
        results.append((f"历史统计 {args.player}", print_response(status, data)))

    if args.run_admin_tests:
        if not args.admin_token or not args.admin_player:
            parser.error("--run-admin-tests 必须同时提供 --admin-token 和 --admin-player")
        print(colored("\n⚠️  即将执行会修改 player-data.json 与 BDS 白名单的管理接口测试", "yellow"))
        admin_requests = [
            ("执行命令", "/admin/command", {"command": args.admin_command, "requester": "api-smoke-test"}),
            ("绑定白名单", "/whitelist/bind", {
                "platform": "api-test", "selfId": "api-test-bot", "userId": "api-test-user",
                "channelId": "api-test-channel", "playerName": args.admin_player,
            }),
            ("解绑白名单", "/whitelist/unbind", {
                "platform": "api-test", "selfId": "api-test-bot", "userId": "api-test-user",
            }),
            ("管理员添加白名单", "/whitelist/add", {
                "playerName": args.admin_player, "requester": "api-smoke-test",
            }),
            ("管理员移除白名单", "/whitelist/remove", {
                "playerName": args.admin_player, "requester": "api-smoke-test",
            }),
        ]
        for name, endpoint, body in admin_requests:
            print_section("🛡️ ", f"[管理] {name}")
            status, data = request_api(
                f"{api_base}{endpoint}",
                args.timeout,
                method="POST",
                body=body,
                bearer_token=args.admin_token,
            )
            results.append((name, print_response(status, data)))
    
    # 打印结果汇总
    print_header("📋 测试结果汇总")
    
    passed = sum(1 for _, ok in results if ok)
    failed = len(results) - passed
    
    for name, ok in results:
        status = colored("✅ 通过", "green") if ok else colored("❌ 失败", "red")
        print(f"  {status} - {name}")
    
    print()
    print(f"总计: {len(results)} 个测试")
    print(f"  {colored(f'✅ 通过: {passed}', 'green')}")
    print(f"  {colored(f'❌ 失败: {failed}', 'red') if failed else f'❌ 失败: {failed}'}")
    
    if failed == 0:
        print(colored("\n🎉 所有测试通过!", "green"))
    else:
        print(colored(f"\n⚠️  有 {failed} 个测试失败", "yellow"))
    
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
