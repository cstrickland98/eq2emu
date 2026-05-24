import os
import sys
import urllib.request


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: vcpkg-download.py <url> <destination>", file=sys.stderr)
        return 2

    url = sys.argv[1]
    destination = sys.argv[2]
    os.makedirs(os.path.dirname(destination), exist_ok=True)

    with urllib.request.urlopen(url, timeout=120) as response:
        with open(destination, "wb") as output:
            while True:
                chunk = response.read(1024 * 1024)
                if not chunk:
                    break
                output.write(chunk)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
