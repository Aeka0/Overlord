"""Summarize explicit H2 sun-shadow evidence; never authorize reuse."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import zipfile


def read_capture(path: Path) -> dict:
    if path.suffix.lower() == ".zip":
        with zipfile.ZipFile(path) as archive:
            matches = [item for item in archive.infolist()
                       if Path(item.filename).name == "sun-shadow.json"]
            if len(matches) != 1 or matches[0].file_size > 8 * 1024 * 1024:
                raise ValueError("expected one sun-shadow.json within the 8 MiB file budget")
            value = json.loads(archive.read(matches[0]).decode("utf-8"))
    else:
        if path.stat().st_size > 8 * 1024 * 1024:
            raise ValueError("sun-shadow.json exceeds the 8 MiB file budget")
        value = json.loads(path.read_text(encoding="utf-8"))
    if value.get("schema") != 1 or not isinstance(value.get("samples"), list):
        raise ValueError("unsupported schema or missing samples")
    if any(sample.get("request") != value.get("request") for sample in value["samples"]):
        raise ValueError("samples belong to another request")
    return value


def draws(eye: dict) -> int:
    return sum(eye.get("per_api", [])[:7])


def notes(sample: dict) -> str:
    result = []
    if not sample.get("pair_complete"):
        result.append("native pair incomplete")
    if sample.get("error") != "none":
        result.append(str(sample.get("error", "missing error state")))
    if sample.get("auxiliary_requested"):
        result.append("auxiliary: initial reuse ineligible")
    eyes = sample.get("eyes", [])
    if len(eyes) != 3:
        return "; ".join(result + ["view evidence missing"])
    for index, eye in enumerate(eyes[:2]):
        prefix = f"eye {index}"
        if eye.get("root_calls") != 1 or not eye.get("data_matches_frontend"):
            result.append(f"{prefix}: root/owner identity incomplete")
        if eye.get("partition_snapshot_truncated") or not 1 <= eye.get("active_partitions", 0) <= 4:
            result.append(f"{prefix}: partition coverage incomplete")
        calls = eye.get("per_api", [])
        if len(calls) != 10 or calls[-1]:
            result.append(f"{prefix}: opaque/missing command-list evidence")
        if not draws(eye):
            result.append(f"{prefix}: no observed draw")
        passes = eye.get("subpasses", [])
        if any(p.get("kind") == 0 for p in passes):
            result.append(f"{prefix}: cache subpath executed")
        if eye.get("dropped_subpasses") or any(p.get("dropped_geometry_views") for p in passes):
            result.append(f"{prefix}: observation capacity exceeded")
        if eye.get("dropped_output_groups") or eye.get("dropped_resource_writes"):
            result.append(f"{prefix}: owner writer observation truncated")
        resources = {item.get("resource") for item in eye.get("outputs", []) if item.get("resource")}
        if any(w.get("phase") != 1 and w.get("destination") in resources
               for w in eye.get("owner_resource_writes", [])):
            result.append(f"{prefix}: shadow resource written outside sun root")
        for group in eye.get("owner_output_groups", []):
            phases = group.get("phases", [0, 0, 0])
            if len(phases) == 3 and (phases[0] or phases[2]):
                if group.get("color_resource") in resources or group.get("depth_resource") in resources:
                    result.append(f"{prefix}: shadow output bound outside sun root")
                    break
    left, right = eyes[:2]
    if (left.get("ordinal_begin"), left.get("ordinal_end")) != (right.get("ordinal_begin"), right.get("ordinal_end")):
        result.append("sun ordinal ranges differ")
    for output, name in enumerate(("main", "transparent")):
        if output and not any(eye.get("transparent_enabled") for eye in eyes[:2]):
            continue
        comparisons = sample.get("left_right", [])
        comparison = comparisons[output] if len(comparisons) > output else {}
        if comparison.get("state") != "equal":
            result.append(f"{name}: {comparison.get('state', 'missing')}")
        elif not all(comparison.get(f"{side}_nonuniform_pixels") for side in ("left", "right")):
            result.append(f"{name}: uniform pixels")
    return "; ".join(result) or "review light inputs, bindings and native side effects"


def render(value: dict) -> str:
    samples = value["samples"]
    started, published = value.get("started_samples", 0), value.get("published_samples", 0)
    lines = ["# Sun-shadow observation", "",
             f"Request {value.get('request')}; observer `{value.get('installation')}`; "
             f"started/published/archived: {started}/{published}/{len(samples)}.", "",
             "Both eyes retained native drawing. This report does not approve reuse or establish an FPS gain.", ""]
    if value.get("installation") != "installed" or not samples:
        lines += ["Observer unavailable or no samples: no equivalence conclusion is possible.", ""]
    if started != published or published != len(samples):
        lines += ["Evidence is incomplete; missing samples are not equality.", ""]
    lines += ["| Pair | Seconds | Aux | Main | Transparent | Draws L/R | Review notes |",
              "| --- | --- | --- | --- | --- | --- | --- |"]
    for sample in samples:
        eyes = sample.get("eyes", [])
        left, right = (eyes + [{}, {}])[:2]
        states = [item.get("state", "missing") for item in sample.get("left_right", [])[:2]]
        states += ["missing"] * (2 - len(states))
        if not left.get("transparent_enabled") and not right.get("transparent_enabled"):
            states[1] = "disabled"
        elapsed = (sample.get("tick", 0) - value.get("started_tick", 0)) / 1000
        lines.append(f"| {sample.get('pair')} | {elapsed:.2f} | {'yes' if sample.get('auxiliary_requested') else 'no'} "
                     f"| {states[0]} | {states[1]} | {draws(left)}/{draws(right)} | {notes(sample)} |")
    lines += ["", f"Auxiliary samples: {sum(bool(s.get('auxiliary_requested')) for s in samples)}. "
              f"Transparent-enabled samples: {sum(any(e.get('transparent_enabled') for e in s.get('eyes', [])[:2]) for s in samples)}.", "",
              "Actual ticks identify coverage; planned phases do not prove operator actions or HMD acceptance. "
              "Inspect raw light/list inputs, resource bindings, index ranges, native cache/job state and contemporaneous status files before implementing a skip."]
    return "\n".join(lines) + "\n"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        report = render(read_capture(args.capture))
    except (OSError, ValueError, KeyError, TypeError, zipfile.BadZipFile) as error:
        parser.exit(1, f"capture analysis failed: {error}\n")
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(report, encoding="utf-8")
    else:
        print(report, end="")


if __name__ == "__main__":
    main()
