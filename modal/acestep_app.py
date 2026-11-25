"""ACE-Step 1.5 on Modal for Reelcut's Songs page.

One-time:  modal run modal/acestep_app.py::download
Deploy:    modal deploy modal/acestep_app.py
"""
import subprocess

import modal

ACE_REPO = "https://github.com/ace-step/ACE-Step-1.5"
ACE_COMMIT = "ca1e85fe9430179831e6bc6be790c332190a3866"
CKPT = "/ckpt"

app = modal.App("reelcut-acestep")
checkpoints = modal.Volume.from_name("reelcut-acestep-checkpoints", create_if_missing=True)

image = (
    modal.Image.from_registry("nvidia/cuda:12.8.1-runtime-ubuntu22.04", add_python="3.11")
    .apt_install("git", "ffmpeg", "libsndfile1", "build-essential")
    .pip_install("uv")
    .run_commands(
        f"git clone {ACE_REPO} /opt/acestep && cd /opt/acestep && git checkout {ACE_COMMIT}",
        "cd /opt/acestep && uv sync --frozen",
    )
    .env({
        "ACESTEP_CHECKPOINTS_DIR": CKPT,
        "ACESTEP_API_HOST": "0.0.0.0",
        "ACESTEP_API_PORT": "8001",
        "ACESTEP_CONFIG_PATH": "acestep-v15-turbo",
        "ACESTEP_LM_MODEL_PATH": "acestep-5Hz-lm-1.7B",
        "ACESTEP_INIT_LLM": "true",
    })
)


@app.function(image=image, volumes={CKPT: checkpoints}, timeout=3600)
def download():
    bin_ = "/opt/acestep/.venv/bin/acestep-download"
    # The main download already includes the turbo DiT, the 1.7B LM, the VAE and the embedder.
    subprocess.run([bin_, "--dir", CKPT], check=True, cwd="/opt/acestep")
    checkpoints.commit()


# L4 first; A10 or L40S when no L4 is free, so a request never waits on one GPU type.
@app.function(image=image, gpu=["L4", "A10", "L40S"], volumes={CKPT: checkpoints},
              max_containers=1, scaledown_window=120, timeout=600)
@modal.concurrent(max_inputs=20)
@modal.web_server(port=8001, startup_timeout=600, requires_proxy_auth=True)
def api():
    subprocess.Popen(["/opt/acestep/.venv/bin/python", "-c", SERVE], cwd="/opt/acestep")


# ACE-Step's own server plus one route, /reelcut/gpu, so Reelcut can price the
# job at the GPU Modal actually picked. uvicorn imports "acestep.api_server:app",
# which is the module object this script has already extended.
SERVE = """
import subprocess
import acestep.api_server as server

@server.app.get("/reelcut/gpu")
def reelcut_gpu():
    out = subprocess.run(["nvidia-smi", "--query-gpu=name", "--format=csv,noheader"],
                         capture_output=True, text=True).stdout.strip()
    return {"gpu": out.splitlines()[0] if out else ""}

server.main()
"""
