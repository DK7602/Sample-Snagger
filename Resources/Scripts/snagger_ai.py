#!/usr/bin/env python3
"""
Sample Snagger - AI stem engine helper.

This script is written into the Sample Snagger tools folder by the plug-in and driven
by it. It has three commands:

    python snagger_ai.py install  --venv DIR        create a private venv with Demucs
    python snagger_ai.py check                      verify torch + demucs import
    python snagger_ai.py separate --input IN.wav --outdir DIR [--model htdemucs]
                                  [--two-stems vocals] [--shifts 1] [--device auto]

Output protocol (one message per line, flushed):
    PROGRESS <0-100>     STATUS <text>     STEM <name>\t<path>     DONE <text>     ERROR <text>
"""

import argparse
import os
import platform
import shutil
import subprocess
import sys

try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass


def say(kind, msg=""):
    print(f"{kind} {msg}", flush=True)


def fail(msg, code=1):
    say("ERROR", msg)
    sys.exit(code)


def venv_python(venv):
    if os.name == "nt":
        return os.path.join(venv, "Scripts", "python.exe")
    return os.path.join(venv, "bin", "python3")


# --------------------------------------------------------------------------------------
# install
# --------------------------------------------------------------------------------------
def torch_index_url():
    """Pick the right PyTorch wheel index for this machine."""
    system = platform.system()
    has_nvidia = shutil.which("nvidia-smi") is not None
    if system == "Darwin":
        return None                       # PyPI wheels (CPU + Apple GPU)
    if has_nvidia:
        return "https://download.pytorch.org/whl/cu126"
    if system == "Linux":
        return "https://download.pytorch.org/whl/cpu"   # avoid multi-GB CUDA wheels
    return None                           # Windows PyPI wheels are CPU-only already


def run_step(cmd, p0, p1, status, check=True):
    say("STATUS", status)
    say("PROGRESS", f"{p0:.0f}")
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True, encoding="utf-8", errors="replace", bufsize=1)
    lines = 0
    tail = []
    for line in proc.stdout:
        line = line.rstrip()
        if not line:
            continue
        tail = (tail + [line])[-15:]
        lines += 1
        # creep the progress bar forward while pip works
        frac = min(0.95, lines / 120.0)
        say("PROGRESS", f"{p0 + (p1 - p0) * frac:.1f}")
        if line.startswith(("Downloading", "Collecting", "Installing collected", "Successfully installed")):
            print(line[:160], flush=True)
    code = proc.wait()
    if code != 0 and check:
        for t in tail:
            print(t, flush=True)
        fail(f"{status} failed (exit code {code}). See the log above.")
    say("PROGRESS", f"{p1:.0f}")
    return code == 0


def cmd_install(args):
    if sys.version_info < (3, 9):
        fail("Python 3.9 or newer is required")

    venv = os.path.abspath(args.venv)
    py = venv_python(venv)

    if not os.path.exists(py):
        say("STATUS", "Creating a private Python environment")
        say("PROGRESS", "2")
        import venv as venvmod
        try:
            venvmod.EnvBuilder(with_pip=True, clear=True).create(venv)
        except Exception as e:  # noqa
            fail(f"Could not create the Python environment: {e}")
    if not os.path.exists(py):
        fail("Python environment was not created correctly")

    run_step([py, "-m", "pip", "install", "--upgrade", "pip", "setuptools", "wheel"], 3, 8, "Updating pip")

    torch_cmd = [py, "-m", "pip", "install", "--upgrade", "torch", "torchaudio"]
    idx = torch_index_url()
    if idx:
        torch_cmd += ["--index-url", idx]
    run_step(torch_cmd, 8, 72, "Installing PyTorch (large download, please wait)")

    ok = run_step([py, "-m", "pip", "install", "demucs", "soundfile", "numpy"], 72, 94,
                  "Installing Demucs", check=False)
    if not ok:
        # Fall back to installing demucs without optional extras that may lack wheels (e.g. lameenc)
        run_step([py, "-m", "pip", "install", "julius", "einops", "openunmix", "pyyaml", "tqdm",
                  "dora-search", "omegaconf", "soundfile", "numpy"], 72, 90, "Installing Demucs dependencies")
        run_step([py, "-m", "pip", "install", "--no-deps", "demucs"], 90, 94, "Installing Demucs")

    say("STATUS", "Checking the AI engine")
    r = subprocess.run([py, os.path.abspath(__file__), "check"], stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace")
    for line in r.stdout.splitlines():
        print(line, flush=True)
    if r.returncode != 0:
        fail("The AI engine installed but failed its self-check")

    say("PROGRESS", "100")
    say("DONE", "AI stem engine ready")


# --------------------------------------------------------------------------------------
# check
# --------------------------------------------------------------------------------------
def cmd_check(_args):
    try:
        import torch  # noqa
        import numpy  # noqa
        import soundfile  # noqa
        import demucs  # noqa
        from demucs.pretrained import get_model  # noqa
        from demucs.apply import apply_model  # noqa
    except Exception as e:  # noqa
        fail(f"import failed: {e}")
    dev = "cuda" if torch.cuda.is_available() else "cpu"
    ver = getattr(demucs, "__version__", "?")
    say("OK", f"demucs {ver} / torch {torch.__version__} / {dev}")


# --------------------------------------------------------------------------------------
# separate
# --------------------------------------------------------------------------------------
def pick_device(requested):
    import torch
    if requested and requested != "auto":
        return requested
    if torch.cuda.is_available():
        return "cuda"
    return "cpu"


def cmd_separate(args):
    say("STATUS", "Starting AI engine")
    say("PROGRESS", "1")
    try:
        import numpy as np
        import soundfile as sf
        import torch
        from demucs.pretrained import get_model
        from demucs.apply import apply_model, BagOfModels
        import demucs.apply as dapply
    except Exception as e:  # noqa
        fail(f"AI engine is not installed correctly ({e}). Open Settings and click Install next to AI Stem Engine.")

    try:
        torch.set_num_threads(max(1, os.cpu_count() or 1))
    except Exception:
        pass

    say("STATUS", f"Loading model {args.model} (first run downloads it)")
    try:
        model = get_model(args.model)
    except Exception as e:  # noqa
        fail(f"Could not load model '{args.model}': {e}")
    model.eval()
    say("PROGRESS", "4")

    try:
        wav, sr = sf.read(args.input, dtype="float32", always_2d=True)
    except Exception as e:  # noqa
        fail(f"Could not read input audio: {e}")

    wav = torch.from_numpy(np.ascontiguousarray(wav.T))
    if wav.shape[0] == 1:
        wav = wav.repeat(2, 1)
    elif wav.shape[0] > 2:
        wav = wav[:2]

    if sr != model.samplerate:
        import julius
        wav = julius.resample_frac(wav, int(sr), int(model.samplerate))

    ref = wav.mean(0)
    mean = ref.mean()
    std = ref.std() + 1e-8
    wav = (wav - mean) / std

    # Progress: demucs wraps its chunk loop in tqdm.tqdm(...) when progress=True.
    shifts = max(1, int(args.shifts))
    n_models = len(model.models) if isinstance(model, BagOfModels) else 1
    total_passes = n_models * shifts
    state = {"done": 0}

    class _ProgressShim:
        @staticmethod
        def tqdm(iterable, **_kw):
            items = list(iterable)
            n = max(1, len(items))
            for i, item in enumerate(items):
                yield item
                frac = (state["done"] + (i + 1) / n) / total_passes
                say("PROGRESS", f"{min(97.0, 5.0 + 92.0 * frac):.1f}")
            state["done"] += 1

    dapply.tqdm = _ProgressShim

    device = pick_device(args.device)
    say("STATUS", f"Separating on {device.upper()}")
    try:
        with torch.no_grad():
            sources = apply_model(model, wav[None], device=device, shifts=shifts,
                                  split=True, overlap=0.25, progress=True)[0]
    except Exception as e:  # noqa
        if device != "cpu":
            say("STATUS", "GPU failed, retrying on CPU")
            with torch.no_grad():
                sources = apply_model(model, wav[None], device="cpu", shifts=shifts,
                                      split=True, overlap=0.25, progress=True)[0]
        else:
            fail(f"Separation failed: {e}")

    sources = sources * std + mean
    names = list(model.sources)

    if args.two_stems:
        if args.two_stems not in names:
            fail(f"This model has no '{args.two_stems}' stem")
        idx = names.index(args.two_stems)
        stem = sources[idx]
        rest = sources.sum(0) - stem
        other_name = "music" if args.two_stems == "vocals" else f"no_{args.two_stems}"
        pairs = [(args.two_stems, stem), (other_name, rest)]
    else:
        pairs = list(zip(names, sources))

    os.makedirs(args.outdir, exist_ok=True)
    say("STATUS", "Writing stems")
    for name, t in pairs:
        path = os.path.join(args.outdir, f"{name}.wav")
        sf.write(path, t.detach().cpu().numpy().T, int(model.samplerate), subtype="FLOAT")
        say("STEM", f"{name}\t{path}")

    say("PROGRESS", "100")
    say("DONE", "ok")


def main():
    ap = argparse.ArgumentParser(description="Sample Snagger AI stem helper")
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("install")
    p.add_argument("--venv", required=True)

    sub.add_parser("check")

    p = sub.add_parser("separate")
    p.add_argument("--input", required=True)
    p.add_argument("--outdir", required=True)
    p.add_argument("--model", default="htdemucs")
    p.add_argument("--two-stems", dest="two_stems", default=None)
    p.add_argument("--shifts", default="1")
    p.add_argument("--device", default="auto")

    args = ap.parse_args()
    {"install": cmd_install, "check": cmd_check, "separate": cmd_separate}[args.cmd](args)


if __name__ == "__main__":
    main()
