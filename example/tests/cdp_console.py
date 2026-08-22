#!/usr/bin/env python3
"""Attach to a Chrome DevTools Protocol endpoint, reload the app page and
capture console output until TEST_PASSED/TEST_FAILED appears.

Usage: cdp_console.py <debug_port> <url_substring> [timeout_seconds]
Exit codes: 0 = TEST_PASSED, 1 = TEST_FAILED, 2 = timeout/other error
"""

import asyncio
import json
import sys
import urllib.request

import websockets


async def main() -> int:
    debug_port = sys.argv[1]
    url_sub = sys.argv[2]
    timeout = float(sys.argv[3]) if len(sys.argv) > 3 else 240.0

    # Find the page target.
    ws_url = None
    for _ in range(30):
        try:
            with urllib.request.urlopen(
                f"http://localhost:{debug_port}/json/list", timeout=2
            ) as r:
                targets = json.load(r)
            for t in targets:
                if t.get("type") == "page" and url_sub in t.get("url", ""):
                    ws_url = t["webSocketDebuggerUrl"]
                    break
            if ws_url:
                break
        except Exception:
            pass
        await asyncio.sleep(1)
    if not ws_url:
        print("CDP: page target not found", flush=True)
        return 2

    async with websockets.connect(ws_url, max_size=50 * 1024 * 1024) as ws:
        mid = 0

        async def send(method, params=None):
            nonlocal mid
            mid += 1
            await ws.send(
                json.dumps({"id": mid, "method": method, "params": params or {}})
            )

        await send("Runtime.enable")
        await send("Page.enable")
        # Reload so we capture the app output from the very start.
        await send("Page.reload", {"ignoreCache": True})

        deadline = asyncio.get_event_loop().time() + timeout
        while True:
            remaining = deadline - asyncio.get_event_loop().time()
            if remaining <= 0:
                print("CDP: TIMEOUT", flush=True)
                return 2
            try:
                msg = await asyncio.wait_for(ws.recv(), timeout=remaining)
            except asyncio.TimeoutError:
                print("CDP: TIMEOUT", flush=True)
                return 2
            data = json.loads(msg)
            method = data.get("method", "")
            if method == "Runtime.consoleAPICalled":
                params = data.get("params", {})
                args = params.get("args", [])
                text = " ".join(
                    str(a.get("value", a.get("description", ""))) for a in args
                )
                print(f"[{params.get('type', '')}] {text}", flush=True)
                if "TEST_PASSED" in text:
                    return 0
                if "TEST_FAILED" in text:
                    return 1
            elif method == "Runtime.exceptionThrown":
                details = data.get("params", {}).get("exceptionDetails", {})
                desc = details.get("text", "")
                exc = details.get("exception", {})
                if exc:
                    desc += " " + str(
                        exc.get("description", exc.get("value", ""))
                    )
                print(f"[exception] {desc}", flush=True)


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
