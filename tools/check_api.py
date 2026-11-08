"""Compare the merged public API with pinned standard V4 and fork V5 headers."""

import argparse
import re
from pathlib import Path


def normalized(text):
    text = re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.S)
    return re.sub(r"\s+", "", text)


def declaration(text, name):
    match = re.search(r"\b(?:class|struct|enum\s+class)\s+" + name + r"\b[^;{]*\{", text)
    if not match:
        raise AssertionError(f"Missing declaration: {name}")
    depth = 1
    end = match.end()
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return normalized(text[match.start():end])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--upstream", type=Path, required=True)
    parser.add_argument("--fork", type=Path, required=True)
    parser.add_argument("--merged", type=Path, default=Path(__file__).resolve().parents[1] / "src/PrecisionAPI.h")
    args = parser.parse_args()
    old, fork, merged = (path.read_text(encoding="utf-8-sig") for path in (args.upstream, args.fork, args.merged))
    names = ["APIResult", "PreHitModifier", "PreHitCallbackReturn", "WeaponCollisionCallbackReturn",
             "PrecisionLayerType", "PrecisionLayerSetupCallbackReturn", "PrecisionHitData",
             "CollisionFilterComparisonResult", "RequestedAttackCollisionType",
             "IVPrecision1", "IVPrecision2", "IVPrecision3", "IVPrecision4"]
    for name in names:
        current = declaration(merged, name)
        if name == "PrecisionHitData":
            current = current.replace("constexprPrecisionHitData()=default;", "")
        assert declaration(old, name) == current, f"Standard API changed: {name}"
    aliases = lambda source: normalized("".join(re.findall(r"\busing\s+\w+\s*=.*?;", source)))
    assert aliases(old) == aliases(merged), "Standard callback aliases changed"
    assert declaration(fork, "IVPrecision5") == declaration(merged, "IVPrecision5"), "Fork V5 virtual ABI changed"
    assert declaration(fork, "InterfaceVersion") == declaration(merged, "InterfaceVersion"), "Interface enum changed"
    print(f"PASS: {len(names)} standard declarations, callback aliases, V5 interface and version enum unchanged")


if __name__ == "__main__":
    main()
