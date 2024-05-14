# Muzero for Enduro

This project is based on modifications of [Reinforcement Learning and Games Lab, Institute of Information Science, Academia Sinica]'s [minizero](https://github.com/rlglab/minizero/tree/main?tab=readme-ov-file).

## Modifications
- Adjust container scripts to be compatible with WSL
- Optimized hyperparameters configuration for Enduro game.

## Prerequisites

MiniZero is designed to run on a Linux platform and requires at least one NVIDIA GPU for operation. To simplify setup, a pre-built [container image](https://hub.docker.com/r/kds285/minizero) that includes all necessary packages is available. Therefore, you will also need a container tool such as Docker or Podman.

## Testing Platform
- **CPU**: Ryzen 7 3700x
- **GPU**: GPU RTX 2070 Super with 8 GB VRAM.
- **RAM**: 15GB.
- **Operating System**: Ubuntu 22.04 LTS.


## Quick Start Guide


### Step 1: Clone the Repository
To get started, clone the repository to your local machine:
```bash=
git clone git@github.com:rlglab/minizero.git
cd minizero # Navigate into the cloned repository
```
### Step 2: Launch the Runtime Environment
Start the runtime environment using a container. Ensure you have either Podman or Docker installed:
```bash=
scripts/start-container.sh # Start the container
```
Once the container is up and running, it will set its working directory to /workspace. Remember to run all following commands inside this container environment.

