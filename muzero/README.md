# Muzero for Enduro

This project is based on modifications of [minizero](https://github.com/rlglab/minizero/tree/main?tab=readme-ov-file) from Reinforcement Learning and Games Lab, Institute of Information Science, Academia Sinica

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
git clone git@github.com:aldrichvalentino/2024-sp-dl-final-project.git
cd minizero # Navigate into the cloned repository
```
### Step 2: Launch the Runtime Environment
Start the runtime environment using a container. Ensure you have either Podman or Docker installed:
```bash=
scripts/start-container.sh # Start the container
```
Once the container is up and running, it will set its working directory to /workspace. Remember to run all following commands inside this container environment.

### Training
```bash=
tools/quick-run.sh train atari gmz 300 -n enduro_nn2_n18 -conf_str program_auto_seed=true:actor_mcts_reward_discount=0.997:actor_mcts_value_rescale=true:actor_resign_threshold=-2:zero_num_games_per_iteration=250:zero_disable_resign_ratio=1:learner_per_init_beta=0.4:learner_per_beta_anneal=false:learner_training_step=200:learner_training_display_step=100:learner_batch_size=512:learner_n_step_return=5:learner_learning_rate=0.1:nn_num_blocks=2:nn_num_hidden_channels=64:nn_num_value_hidden_channels=64:env_atari_name=enduro:learner_num_thread=12:zero_replay_buffer=5:learner_use_per=true
```

