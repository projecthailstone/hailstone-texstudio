"""Converts the texture studio's PyTorch models to ncnn (README.md "Models"), once, on a developer's machine.

    python -I convert_models.py MODELDIR OUTDIR

MODELDIR holds the PBRify_Remix release's 4x-PBRify_UpscalerSPANV4.pth, 1x-PBRify_NormalV3.pth and
1x-PBRify_RoughnessV2.pth (CC0, github.com/Kim2091/PBRify_Remix). OUTDIR gets <id>.param and <id>.bin for each, the
files the studio loads; point the build's TEXSTUDIO_MODELS at it. Needs torch, spandrel and pnnx (pip); the
models are traced at 64x64 and 96x80 so ncnn takes any size.
"""

import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import spandrel
import torch

MODELS = ["4x-PBRify_UpscalerSPANV4", "1x-PBRify_NormalV3", "1x-PBRify_RoughnessV2"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("models", type=Path)
    ap.add_argument("out", type=Path)
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    pnnx = shutil.which("pnnx") or str(Path(sys.executable).parent / "pnnx")
    for name in MODELS:
        model = spandrel.ModelLoader().load_from_file(str(args.models / f"{name}.pth")).model.eval()
        with tempfile.TemporaryDirectory() as tmp:
            traced = Path(tmp) / "model.pt"
            with torch.no_grad():
                torch.jit.trace(model, torch.rand(1, 3, 64, 64)).save(str(traced))
            subprocess.run([pnnx, str(traced), "inputshape=[1,3,64,64]f32", "inputshape2=[1,3,96,80]f32", "fp16=0"],
                           cwd=tmp, check=True, stdout=subprocess.DEVNULL)
            for ext in ("param", "bin"):
                shutil.copyfile(Path(tmp) / f"model.ncnn.{ext}", args.out / f"{name}.{ext}")
        print(f"{name}: done")


if __name__ == "__main__":
    main()
