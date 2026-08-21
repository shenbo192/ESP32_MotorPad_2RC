#!/usr/bin/env python3
"""Send JavaScript code to the EasyEDA WebSocket Bridge Server HTTP API and print the result."""
import sys, json, urllib.request, urllib.error

PORT = 49620
URL = f"http://127.0.0.1:{PORT}/execute"

def main():
    if len(sys.argv) > 1:
        code = sys.argv[1]
    else:
        code = sys.stdin.read()
    payload = json.dumps({"code": code}).encode("utf-8")
    req = urllib.request.Request(
        URL, data=payload, headers={"Content-Type": "application/json"}, method="POST"
    )
    try:
        with urllib.request.urlopen(req, timeout=300) as r:
            print(r.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        sys.stderr.write("HTTPError: " + e.read().decode("utf-8", "replace") + "\n")
        sys.exit(1)
    except Exception as e:
        sys.stderr.write("Error: " + str(e) + "\n")
        sys.exit(1)

if __name__ == "__main__":
    main()
